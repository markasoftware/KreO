import copy
from io import TextIOWrapper
from pathlib import Path
from typing import Any, Callable

from parseconfig import Config
from postgame import analysis_results as ar
from postgame.analysis_results import MethodType

BASE_ADDR = 0x400000


def get_gt_analysis_results_instrumented(
    gt_methods_instrumented: set[int],
    gt_class_info_list: ar.AnalysisResults,
) -> ar.AnalysisResults:
    """
    Return a list of classes from the list of ground truth classes that
    were instrumented during dynamic analysis. The classes instrumented
    must have at least one method that was instrumented. Only methods that
    are instrumented are included in the new classes.

    @param gt_methods_instrumented Set of instrumented methods addresses.
    @param gt_class_info_list List of all class.
    @return New list of classes. Each class only contains methods in the
    gt_methods_instrumented set. The gt_class_info_list is left unmodified.
    """
    gt_class_info_instrumented: ar.AnalysisResults = []

    for ci in gt_class_info_list:
        new_method_set: set[ar.Method] = set()
        for mi in ci.method_set:
            if mi.address in gt_methods_instrumented:
                new_method_set.add(mi)
        if len(new_method_set) > 0:
            instrumented_ci = copy.deepcopy(ci)
            instrumented_ci.method_set = new_method_set
            gt_class_info_instrumented.append(instrumented_ci)

    return gt_class_info_instrumented


def get_method_set_by_type(
    analysis_results: ar.AnalysisResults,
    type_to_match: str | None,
) -> set[ar.Method]:
    """
    @return A set of all methods in the classes list that have the given
    type. If type_to_match is None, all methods will be returned.
    """
    methods_with_type: set[ar.Method] = set()

    for cls in analysis_results.structures.values():
        for method in cls.methods.values():
            if type_to_match is None or method.type == type_to_match:
                methods_with_type.add(method)

    return methods_with_type


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
    ctor_set = get_method_set_by_type(ground_truth, MethodType.ctor)
    dtor_set = get_method_set_by_type(ground_truth, MethodType.dtor)

    ctor_instrumented = 0
    dtor_instrumented = 0

    for mi in ctor_set:
        if mi.ea in gt_methods_instrumented_set:
            ctor_instrumented += 1

    for mi in dtor_set:
        if mi.ea in gt_methods_instrumented_set:
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
) -> tuple[int, int, int]:
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

    return tp, fp, fn


def evaluate_methods(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> tuple[int, int, int]:
    """
    Computes and returns precision and recall of the methods in the
    ground truth and generated data.

    Methods are equal if their address is equal.
    """
    gt_methods = get_method_set_by_type(gt_analysis_results, None)
    gt_methods = set(map(lambda x: x.ea, gt_methods))

    gen_methods = get_method_set_by_type(gen_analysis_results, None)
    gen_methods = set(map(lambda x: x.ea, gen_methods))

    tp = intersection_size(
        gen_methods,
        gt_methods,
    )

    fn = false_negatives(gt_methods, tp)
    fp = false_positives(gen_methods, tp)

    return tp, fp, fn


def evaluate_specific_type(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
    t: MethodType,
) -> tuple[int, int, int]:
    """
    Compares the set of methods that have the same type (t),
    returning the precision and recall.
    """
    ground_truth_type = get_method_set_by_type(gt_analysis_results, t)
    ground_truth_type = set(map(lambda x: x.ea, ground_truth_type))
    generated_data_type = get_method_set_by_type(gen_analysis_results, t)
    generated_data_type = set(map(lambda x: x.ea, generated_data_type))

    tp = intersection_size(generated_data_type, ground_truth_type)

    fn = false_negatives(ground_truth_type, tp)
    fp = false_positives(generated_data_type, tp)

    return tp, fp, fn


def evaluate_constructors(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> tuple[int, int, int]:
    return evaluate_specific_type(
        gt_analysis_results,
        gen_analysis_results,
        MethodType.ctor,
    )


def evaluate_destructors(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> tuple[int, int, int]:
    return evaluate_specific_type(
        gt_analysis_results,
        gen_analysis_results,
        MethodType.dtor,
    )


def evaluate_methods_assigned_correct_class(
    gt_analysis_results: ar.AnalysisResults, gen_analysis_results: ar.AnalysisResults
) -> tuple[int, int, int]:
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

    class EvaluationResults:
        """
        Structure containing results associated with a particular generated class.
        """

        def __init__(
            self, true_positives: int, false_positives: int, false_negatives: int
        ):
            self.true_positives = true_positives
            self.false_positives = false_positives
            self.false_negatives = false_negatives

        def __str__(self):
            return "tp: {} fp: {} fn: {}".format(
                self.true_positives, self.false_positives, self.false_negatives
            )

    gen_to_gt_classes = match_gen_to_gt_classes(
        gt_analysis_results, gen_analysis_results
    )

    results: list[EvaluationResults] = []

    for gen_cls, gt_cls in gen_to_gt_classes:
        gen_cls_addrs = set(map(lambda x: x.ea, gen_cls.method_set))
        gt_cls_addrs = set(map(lambda x: x.ea, gt_cls.method_set))

        true_positives = len(gen_cls_addrs.intersection(gt_cls_addrs))
        false_positives = len(gen_cls_addrs) - true_positives
        false_negatives = len(gt_cls_addrs) - true_positives

        results.append(
            EvaluationResults(true_positives, false_positives, false_negatives)
        )

    return (
        sum(map(lambda x: x.true_positives, results)),
        sum(map(lambda x: x.false_positives, results)),
        sum(map(lambda x: x.false_negatives, results)),
    )


def get_gen_gt_cls_pair(
    gen_cls_mangled_name: str,
    matched_classes: set[tuple[ClassInfo, ClassInfo]],
) -> tuple[ClassInfo, ClassInfo]:
    for gen_gt_cls_pair in matched_classes:
        if gen_gt_cls_pair[0].mangled_name == gen_cls_mangled_name:
            return gen_gt_cls_pair
    return None


def evaluate_class_graph_ancestors(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> tuple[int, int, int]:
    """
    Computes the precision and recall, comparing class ancestors in the class
    hierarchy tree. If an ancestor exists in the generated class hierarchy and
    a matching class exists as a parent class for the ground truth class, this
    is a true positive.
    """
    matched_classes = match_gen_to_gt_classes(gt_analysis_results, gen_analysis_results)

    def get_cls_from_data(data: ar.AnalysisResults, mangled_name: str) -> ClassInfo:
        """Get ClassInfo object with associated mangled name from data.
        Returns None if no classes in the data have the given mangled name."""
        for cls in data:
            if cls.mangled_name == mangled_name:
                return cls
        return None

    def get_gen_cls(gen_cls_mangled_name: str) -> ClassInfo:
        """Get ClassInfo object with associated mangled name from generated data."""
        return get_cls_from_data(gen_analysis_results, gen_cls_mangled_name)

    def get_gt_cls(gt_cls_mangled_name: str) -> ClassInfo:
        """Get ClassInfo object with associated mangled name from ground truth."""
        return get_cls_from_data(gt_analysis_results, gt_cls_mangled_name)

    true_positives = 0
    gen_size = 0
    gt_size = 0

    for gen_cls, gt_cls in matched_classes:
        if (
            len(gen_cls.parent_mangled_names) == 0
            and len(gt_cls.parent_mangled_names) == 0
        ):
            # Both gen and ground truth share the "root" i.e. there are no
            # inheritance relationships and that has been correctly identified.
            true_positives += 1
            gen_size += 1
            gt_size += 1
        else:

            def find_ancestors_gen_cls(cls: ClassInfo) -> set[ClassInfo]:
                """
                Find the given class's ancestors. The cls should be a generated class.
                Return a set of ancestor ClassInfo objects.
                """
                ancestors: set[ClassInfo] = set()

                # List of ancestors that will be systematically removed from this
                # worklist and whose associated ClassInfo will be added to the set
                # of ancestors.
                worklist = copy.deepcopy(cls.parent_mangled_names)

                # Find all ancestors of gen class
                while len(worklist) != 0:
                    cls_name = worklist.pop()

                    ci = get_gen_cls(cls_name)
                    if ci != None and ci not in ancestors:
                        ancestors.add(ci)
                        worklist.extend(ci.parent_mangled_names)

                return ancestors

            gen_ancestors = find_ancestors_gen_cls(gen_cls)

            # Find all gt parent classes for the given gt class
            gt_parents: set[ClassInfo] = set()
            for cls_name in gt_cls.parent_mangled_names:
                parent_gt_cls = get_gt_cls(cls_name)
                if parent_gt_cls != None:
                    gt_parents.add(parent_gt_cls)

            # Check if any of the ancestors match the gt
            for gen_ancestor in gen_ancestors:
                # Find the ground truth class associated with the ancestor.
                gen_gt_pair = get_gen_gt_cls_pair(
                    gen_ancestor.mangled_name, matched_classes
                )

                if gen_gt_pair is not None and gen_gt_pair[1] in gt_parents:
                    true_positives += 1

            gen_size += len(gen_ancestors)
            gt_size += len(gt_parents)

    false_negatives = gt_size - true_positives
    false_positives = gen_size - true_positives

    return true_positives, false_positives, false_negatives


def evaluate_class_graph_edges(
    gt_analysis_results: ar.AnalysisResults,
    gen_analysis_results: ar.AnalysisResults,
) -> tuple[int, int, int]:
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

    return tp, fp, fn


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
    ground_truth_excluding_empty = nonempty_classes(gt_analysis_results)
    generated_data_excluding_empty = nonempty_classes(gen_analysis_results)

    matched_classes: set[tuple[ClassInfo, ClassInfo]] = set()

    gt_classes_referenced: set[ClassInfo] = set()

    for gen_cls in generated_data_excluding_empty:
        # Find ground truth classes that have method sets
        # intersecting with the current generated class.
        # For each ground truth class that does have a nonzero
        # method set intersection size, record the size.
        gen_gt_intersection_sizes: Dict[int, ar.AnalysisResults] = dict()
        for gt_cls in ground_truth_excluding_empty:
            if gt_cls in gt_classes_referenced:
                continue

            gen_cls_method_set = set(map(lambda x: x.address, gen_cls.method_set))
            gt_cls_method_set = set(map(lambda x: x.address, gt_cls.method_set))
            s = intersection_size(gen_cls_method_set, gt_cls_method_set)
            if s != 0:
                if s not in gen_gt_intersection_sizes:
                    gen_gt_intersection_sizes[s] = []
                gen_gt_intersection_sizes[s].append(gt_cls)

        # Get largest method set intersection.
        intersection_sizes = list(
            sorted(gen_gt_intersection_sizes.keys(), reverse=True)
        )
        if intersection_sizes != []:
            gt_cls = gen_gt_intersection_sizes[intersection_sizes[0]][0]
            gt_classes_referenced.add(gt_cls)
            matched_classes.add((gen_cls, gt_cls))

    return matched_classes


import json


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

        def RunTest(
            name: str,
            test: Callable[
                [ar.AnalysisResults, ar.AnalysisResults],
                tuple[int, int, int],
            ],
        ):
            true_positives, false_positives, false_negatives = test(
                gt_analysis_results,
                gen_analysis_results,
            )
            file.write(
                "{}&{}&{}&{}\n".format(
                    name, true_positives, false_positives, false_negatives
                )
            )

        RunTest(
            "Methods Assigned to Correct Class",
            evaluate_methods_assigned_correct_class,
        )
        RunTest("Individual Classes", evaluate_classes)
        RunTest("Constructors", evaluate_constructors)
        RunTest("Destructors", evaluate_destructors)
        RunTest("Methods", evaluate_methods)
        RunTest("Class Graph Edges", evaluate_class_graph_edges)
        RunTest("Class Graph Ancestors", evaluate_class_graph_ancestors)

    with results_path.open() as gt_out:
        run_all_tests(gt_analysis_results, gt_out)

    # if gt_methods_instrumented_path is not None:
    #     gt_class_info_instrumented_list = get_gt_class_info_instrumented(
    #         gt_methods_instrumented, gt_class_info_list
    #     )

    #     with open(results_instrumented_path, "w") as gt_out_instrumented:
    #         run_all_tests(gt_class_info_instrumented_list, gt_out_instrumented)


def main(cfg: Config):
    run_evaluation(
        cfg.gt_results_json,
        cfg.results_json,
        cfg.results_path,
        cfg.results_instrumented_path,
        cfg.gt_methods_instrumented_path,
    )
