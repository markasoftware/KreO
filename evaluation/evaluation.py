import copy
import json
from collections import defaultdict
from dataclasses import dataclass
from io import TextIOWrapper
from pathlib import Path
from typing import Any, Callable

from parseconfig import Config
from postgame import analysis_results as ar
from postgame.analysis_results import MethodType

BASE_ADDR = 0x400000


@dataclass
class EvaluationResults:
    """
    Structure containing results associated with a particular generated class.
    """

    true_positives: int
    false_positives: int
    false_negatives: int

    def __str__(self):
        return "tp: {} fp: {} fn: {}".format(
            self.true_positives, self.false_positives, self.false_negatives
        )


def get_gt_analysis_results_instrumented(
    gt_methods_instrumented: set[str],
    gt_analysis_results: ar.AnalysisResults,
) -> ar.AnalysisResults:
    """
    Return an AnalysisResults model containing only methods in gt_methods_instrumented.
    Empty classes are removed from the results.
    """
    gt_analysis_results_instrumented = gt_analysis_results.copy(deep=True)

    for cls_name, cls in gt_analysis_results_instrumented.structures.items():
        instrumented_methods = {
            method_name: method
            for method_name, method in cls.methods.items()
            if method.ea in gt_methods_instrumented
        }
        cls.methods.clear()
        cls.methods.update(instrumented_methods)
        gt_analysis_results_instrumented.structures[cls_name] = cls

    gt_analysis_results_instrumented.structures = {
        name: cls
        for name, cls in gt_analysis_results_instrumented.structures.items()
        if len(cls.methods) > 0
    }

    return gt_analysis_results_instrumented


def get_method_ea_set_by_type(
    analysis_results: ar.AnalysisResults,
    type_to_match: str | None,
) -> set[str]:
    """
    @return A set of all methods in the classes list that have the given
    type. If type_to_match is None, all methods will be returned.
    """
    method_eas_with_type: set[str] = set()

    for cls in analysis_results.structures.values():
        for method in cls.methods.values():
            if type_to_match is None or method.type == type_to_match:
                method_eas_with_type.add(method.ea)

    return method_eas_with_type


def get_gt_methods_instrumented_set(gt_methods_instrumented_path: Path) -> set[str]:
    """
    @param gt_methods_instrumented_path Path to file containing a list
    of instrumented methods.
    @return Set of method addresses in the ground truth that were instrumented.
    """
    gt_methods_instrumented_set: set[str] = set()
    for line in gt_methods_instrumented_path.open():
        addr = int(line, 16)
        gt_methods_instrumented_set.add(hex(addr + BASE_ADDR))
    return gt_methods_instrumented_set


def load_and_record_gt_method_stats(
    gt_methods_instrumented_set: set[str],
    ground_truth: ar.AnalysisResults,
    gt_method_stats_path: Path,
) -> None:
    """
    Records statistics about the number of methods, constructors, and
    destructors.

    @param gt_methods_instrumented_set Set of method addresses that were
    instrumented by dynamic analysis.
    @param ground_truth List of class info objects in the ground truth.
    @param gt_method_stats_path Name of file to write method stats to.
    """
    ctor_set = get_method_ea_set_by_type(ground_truth, MethodType.ctor)
    dtor_set = get_method_ea_set_by_type(ground_truth, MethodType.dtor)

    ctor_instrumented = 0
    dtor_instrumented = 0

    for mi in ctor_set:
        if mi in gt_methods_instrumented_set:
            ctor_instrumented += 1

    for mi in dtor_set:
        if mi in gt_methods_instrumented_set:
            dtor_instrumented += 1

    gt_methods = 0
    for cls in ground_truth.structures.values():
        gt_methods += len(cls.methods)

    gt_coverage_all_methods = (
        float(len(gt_methods_instrumented_set)) / float(gt_methods)
        if gt_methods != 0
        else 0
    )
    gt_coverage_ctor = (
        float(ctor_instrumented) / float(len(ctor_set)) if len(ctor_set) != 0 else 0.0
    )
    gt_coverage_dtor = (
        float(dtor_instrumented) / float(len(dtor_set)) if len(dtor_set) != 0 else 0.0
    )

    with gt_method_stats_path.open("w") as gt_method_info:
        gt_method_info.write(
            "all method coverage: {:.2}, ctor coverage: {:.2}, dtor coverage: {:.2}".format(
                gt_coverage_all_methods, gt_coverage_ctor, gt_coverage_dtor
            )
        )


# Helper methods provided for ease of readability.


def false_negatives(gt: Any, true_positives: int) -> int:
    return len(gt) - true_positives


def false_positives(gen_data: Any, true_positives: int) -> int:
    return len(gen_data) - true_positives


def intersection_size(l1: set[Any], l2: set[Any]) -> int:
    """
    @return the length of set that contains the intersection of the two sets.
    """
    return len(l1.intersection(l2))


def nonempty_classes(analysis_results: ar.AnalysisResults) -> list[ar.Structure]:
    """
    @return The classes in the given data set that have a nonempty method set.
    """
    return [x for x in analysis_results.structures.values() if len(x.methods) != 0]


def evaluate_classes(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> EvaluationResults:
    """
    Computes and returns the precision and recall, comparing the ground
    truth and generated classes.

    Two classes are equal if the intersection of their method sets is not
    empty. Exclude classes without methods. Ground truth classes can't be
    double counted as a match for multiple generated classes.
    """
    ground_truth_excluding_empty = nonempty_classes(gt_analysis_results)
    generated_data_excluding_empty = nonempty_classes(gen_analysis_results)

    matched_classes = match_gen_to_gt_classes(gt_analysis_results, gen_analysis_results)
    tp = len(matched_classes)

    fn = false_negatives(ground_truth_excluding_empty, tp)
    fp = false_positives(generated_data_excluding_empty, tp)

    return EvaluationResults(tp, fp, fn)


def evaluate_methods(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> EvaluationResults:
    """
    Computes and returns precision and recall of the methods in the
    ground truth and generated data.

    Methods are equal if their address is equal.
    """
    gt_methods = get_method_ea_set_by_type(gt_analysis_results, None)
    gen_methods = get_method_ea_set_by_type(gen_analysis_results, None)

    tp = intersection_size(
        gen_methods,
        gt_methods,
    )

    fn = false_negatives(gt_methods, tp)
    fp = false_positives(gen_methods, tp)

    return EvaluationResults(tp, fp, fn)


def evaluate_specific_type(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
    t: MethodType,
) -> EvaluationResults:
    """
    Compares the set of methods that have the same type (t),
    returning the precision and recall.
    """
    ground_truth_type = get_method_ea_set_by_type(gt_analysis_results, t)
    generated_data_type = get_method_ea_set_by_type(gen_analysis_results, t)

    tp = intersection_size(generated_data_type, ground_truth_type)

    fn = false_negatives(ground_truth_type, tp)
    fp = false_positives(generated_data_type, tp)

    return EvaluationResults(tp, fp, fn)


def evaluate_constructors(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> EvaluationResults:
    return evaluate_specific_type(
        gt_analysis_results,
        gen_analysis_results,
        MethodType.ctor,
    )


def evaluate_destructors(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> EvaluationResults:
    return evaluate_specific_type(
        gt_analysis_results,
        gen_analysis_results,
        MethodType.dtor,
    )


def evaluate_methods_assigned_correct_class(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> EvaluationResults:
    """
    Computes the precision and recall, where for each generated class, compare
    the method set to all ground truth method sets. Compute the F-1 score for
    each ground truth method set (where a true positive is when ground truth and
    generated method sets have the same method) and take the precision and
    recall scores associated with the highest F-1 score as the precision and
    recall for the particular generated class. The final precision/recall is the
    weighted sum of the scores for each generated class (weighted by generated
    class method set size).
    """

    gen_to_gt_classes = match_gen_to_gt_classes(
        gt_analysis_results,
        gen_analysis_results,
    )

    results: list[EvaluationResults] = []

    for gen_cls, gt_cls in gen_to_gt_classes:
        gen_cls_addrs = set(map(lambda x: x.ea, gen_cls.methods.values()))
        gt_cls_addrs = set(map(lambda x: x.ea, gt_cls.methods.values()))

        true_positives = len(gen_cls_addrs.intersection(gt_cls_addrs))
        false_positives = len(gen_cls_addrs) - true_positives
        false_negatives = len(gt_cls_addrs) - true_positives

        results.append(
            EvaluationResults(true_positives, false_positives, false_negatives),
        )

    return EvaluationResults(
        sum(map(lambda x: x.true_positives, results)),
        sum(map(lambda x: x.false_positives, results)),
        sum(map(lambda x: x.false_negatives, results)),
    )


def get_gen_gt_cls_pair(
    gen_cls_mangled_name: str,
    matched_classes: set[tuple[ar.Structure, ar.Structure]],
) -> tuple[ar.Structure, ar.Structure] | None:
    for gen_gt_cls_pair in matched_classes:
        if gen_gt_cls_pair[0].name == gen_cls_mangled_name:
            return gen_gt_cls_pair
    return None


def get_cls_from_data(
    data: ar.AnalysisResults, mangled_name: str
) -> ar.Structure | None:
    """Get ClassInfo object with associated mangled name from data.
    Returns None if no classes in the data have the given mangled name."""
    for cls in data.structures.values():
        if cls.name == mangled_name:
            return cls
    return None


def evaluate_class_graph_ancestors(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> EvaluationResults:
    """
    Computes the precision and recall, comparing class ancestors in the class
    hierarchy tree. If an ancestor exists in the generated class hierarchy and
    a matching class exists as a parent class for the ground truth class, this
    is a true positive.
    """

    def get_gen_cls(gen_cls_mangled_name: str) -> ar.Structure | None:
        """Get ClassInfo object with associated mangled name from generated data."""
        return get_cls_from_data(gen_analysis_results, gen_cls_mangled_name)

    def get_gt_cls(gt_cls_mangled_name: str) -> ar.Structure | None:
        """Get ClassInfo object with associated mangled name from ground truth."""
        return get_cls_from_data(gt_analysis_results, gt_cls_mangled_name)

    matched_classes = match_gen_to_gt_classes(gt_analysis_results, gen_analysis_results)

    tp = 0
    gen_size = 0
    gt_size = 0

    for gen_cls, gt_cls in matched_classes:
        gen_parents = [
            x.name for x in list(filter(lambda x: x.parent, gen_cls.members.values()))
        ]
        gt_parents = [
            x.name for x in list(filter(lambda x: x.parent, gt_cls.members.values()))
        ]

        if len(gen_parents) == 0 and len(gt_parents) == 0:
            # Both gen and ground truth share the "root" i.e. there are no
            # inheritance relationships and that has been correctly identified.
            tp += 1
            gen_size += 1
            gt_size += 1
        else:
            gen_ancestors: set[ar.Structure] = set()

            # List of ancestors that will be systematically removed from this
            # worklist and whose associated ClassInfo will be added to the set
            # of ancestors.
            worklist = copy.deepcopy(gen_parents)

            # Find all ancestors of gen class
            while len(worklist) != 0:
                parent_cls_name = worklist.pop()

                parent_cls = get_gen_cls(parent_cls_name)
                if parent_cls is not None and parent_cls not in gen_ancestors:
                    gen_ancestors.add(parent_cls)
                    worklist.extend(
                        [
                            x.name
                            for x in list(
                                filter(lambda x: x.parent, parent_cls.members.values())
                            )
                        ]
                    )

            # Find all gt parent classes for the given gt class
            gt_parent_classes: set[ar.Structure] = set()
            for cls_name in gt_parents:
                parent_gt_cls = get_gt_cls(cls_name)
                if parent_gt_cls is not None:
                    gt_parent_classes.add(parent_gt_cls)

            # Check if any of the ancestors match the gt
            for gen_ancestor in gen_ancestors:
                # Find the ground truth class associated with the ancestor.
                gen_gt_pair = get_gen_gt_cls_pair(
                    gen_ancestor.name,
                    matched_classes,
                )

                if gen_gt_pair is not None and gen_gt_pair[1].name in gt_parents:
                    tp += 1

            gen_size += len(gen_ancestors)
            gt_size += len(gt_parents)

    fn = gt_size - tp
    fp = gen_size - tp

    return EvaluationResults(tp, fp, fn)


def evaluate_class_graph_edges(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> EvaluationResults:
    """
    Similar to PrecisionAndRecallClassGraphAncestors, except instead of
    comparing ancestors in the class graph, directly compare edges in the graph.
    A true positive is if the generated and ground truth class graphs have a
    matching edge between two of the same classes.
    """
    matched_classes = match_gen_to_gt_classes(gt_analysis_results, gen_analysis_results)

    tp = 0
    gt_size = 0
    gen_size = 0

    for gen_cls, gt_cls in matched_classes:
        # Count number of parents that the two classes share. The "ground truth"
        # for this measure is the total number of inheritance relationships. A true
        # positive would be when the generated and ground truth class share the
        # same inheritance relationship

        # Note: we don't expect parent names to be the same - instead we expect the
        # paired classes to be the same.
        if (
            len(gen_cls.parent_mangled_names) == 0
            and len(gt_cls.parent_mangled_names) == 0
        ):
            # Both gen and ground truth share the "root" i.e. there are no
            # inheritance relationships and that has been correctly identified.
            tp += 1

            gen_size += 1
            gt_size += 1
        else:
            for parent_name in gen_cls.parent_mangled_names:
                gen_gt_pair = get_gen_gt_cls_pair(parent_name, matched_classes)
                if gen_gt_pair != None:
                    # Find gt cls mangled name from gen_gt_pair in gt_cls.parent_mangled_names
                    for name in gt_cls.parent_mangled_names:
                        if name == gen_gt_pair[1].mangled_name:
                            tp += 1
                            break
                else:
                    # If gen_gt_pair is None, this indicates a failure to find
                    # the class associated with the parent in the ground truth
                    # data.
                    print(
                        f"Failed to find parent in generated data called {parent_name} because no ground truth class associated with the parent class."
                    )

        gen_size += len(gen_cls.parent_mangled_names)
        gt_size += len(gt_cls.parent_mangled_names)

    fn = gt_size - tp
    fp = gen_size - tp

    return EvaluationResults(tp, fp, fn)


def match_gen_to_gt_classes(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> set[tuple[ar.Structure, ar.Structure]]:
    """
    Helper method that attempts to match ground truth and generated classes.
    Method sets are used to determine which classes match. Two classes match if
    they share methods from their method sets.

    If multiple ground truth classes exist with methods in a single ground truth
    method set, the ground truth class with more matching methods in its method
    set is chosen as the matched class.
    """
    gt_nonempty_classes = nonempty_classes(gt_analysis_results)
    gen_nonempty_classes = nonempty_classes(gen_analysis_results)

    matched_classes: set[tuple[ar.Structure, ar.Structure]] = set()

    gt_classes_referenced: set[ar.Structure] = set()

    for gen_cls in gen_nonempty_classes:
        # Find ground truth classes that have method sets
        # intersecting with the current generated class.
        # For each ground truth class that does have a nonzero
        # method set intersection size, record the size.
        gen_gt_intersection_sizes: dict[int, list[ar.Structure]] = defaultdict(list)
        for gt_cls in gt_nonempty_classes:
            if gt_cls in gt_classes_referenced:
                continue

            gen_cls_method_set = set(map(lambda x: x.ea, gen_cls.methods.values()))
            gt_cls_method_set = set(map(lambda x: x.ea, gt_cls.methods.values()))
            intersect_size = intersection_size(gen_cls_method_set, gt_cls_method_set)
            if intersect_size != 0:
                gen_gt_intersection_sizes[intersect_size].append(gt_cls)

        # Get largest method set intersection.
        intersection_sizes = list(
            sorted(gen_gt_intersection_sizes.keys(), reverse=True)
        )
        if intersection_sizes != []:
            gt_cls = gen_gt_intersection_sizes[intersection_sizes[0]][0]
            gt_classes_referenced.add(gt_cls)
            matched_classes.add((gen_cls, gt_cls))

    return matched_classes


def run_evaluation(
    gt_class_info_path: Path,
    gen_class_info_path: Path,
    results_path: Path,
    results_instrumented_path: Path,
    gt_methods_instrumented_path: Path,
):
    with gt_class_info_path.open() as f:
        gt_analysis_results = ar.AnalysisResults(**json.load(f))
    with gen_class_info_path.open() as f:
        gen_analysis_results = ar.AnalysisResults(**json.load(f))
    with gt_methods_instrumented_path.open() as f:
        gt_methods_instrumented = get_gt_methods_instrumented_set(
            gt_methods_instrumented_path,
        )

    load_and_record_gt_method_stats(
        gt_methods_instrumented,
        gt_analysis_results,
        gt_methods_instrumented_path.with_name(
            gt_methods_instrumented_path.name + ".stats"
        ),
    )

    def run_all_tests(gt_analysis_results: ar.AnalysisResults, file: TextIOWrapper):
        """
        Runs all tests and writes results to the given file.
        """

        def run_test(
            name: str,
            test: Callable[
                [ar.AnalysisResults, ar.AnalysisResults],
                EvaluationResults,
            ],
        ):
            evaluation_results = test(
                gt_analysis_results,
                gen_analysis_results,
            )
            file.write(
                "{}&{}&{}&{}\n".format(
                    name,
                    evaluation_results.true_positives,
                    evaluation_results.false_positives,
                    evaluation_results.false_negatives,
                )
            )

        run_test(
            "Methods Assigned to Correct Class",
            evaluate_methods_assigned_correct_class,
        )
        run_test("Individual Classes", evaluate_classes)
        run_test("Constructors", evaluate_constructors)
        run_test("Destructors", evaluate_destructors)
        run_test("Methods", evaluate_methods)
        run_test("Class Graph Edges", evaluate_class_graph_edges)
        run_test("Class Graph Ancestors", evaluate_class_graph_ancestors)

    with results_path.open() as gt_out:
        run_all_tests(gt_analysis_results, gt_out)

    gt_instrumented_analysis_results = get_gt_analysis_results_instrumented(
        gt_methods_instrumented,
        gt_analysis_results,
    )

    with open(results_instrumented_path, "w") as gt_out_instrumented:
        run_all_tests(gt_instrumented_analysis_results, gt_out_instrumented)


def main(cfg: Config):
    run_evaluation(
        cfg.gt_results_json,
        cfg.results_json,
        cfg.results_path,
        cfg.results_instrumented_path,
        cfg.gt_methods_instrumented_path,
    )
