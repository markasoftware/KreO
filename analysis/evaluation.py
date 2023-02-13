import json5
import sys
import os
import copy
import pathlib
from typing import List, Set, Dict, Tuple

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..'))

from parseconfig import config

kConstructorType = 'ctor'
kDestructorType = 'dtor'
kBaseAddr = 0x400000

class MethodInfo:
    '''
    Object that contains an address and type associated with a particular method.
    '''
    def __init__(self, address: int, type: str):
        self.address = address
        self.type = type

    def __str__(self):
        return f'(ea: {hex(self.address)}, {self.type})'

    def __eq__(self, o):
        if o is None:
            return False
        return self.address == o.address and self.type == o.type

    def __hash__(self):
        return hash(self.__str__())

class ClassInfo:
    '''
    Object that contains information specific to an object. This includes the object's name,
    the names of object's parents, and a set of methods that the object owns.
    '''
    def __init__(self, mangled_name: str, parent_mangled_names: List[str], method_set: Set[MethodInfo]):
        self.mangled_name = mangled_name
        self.parent_mangled_names = parent_mangled_names
        self.method_set = method_set

    def __str__(self):
        return f'(name: {self.mangled_name}, parents: {[str(x) for x in self.parent_mangled_names]}, methods: {[str(x) for x in self.method_set]})'

    def __eq__(self, o):
        if o == None:
            return False
        return self.mangled_name == o.mangled_name and\
               self.parent_mangled_names == o.parent_mangled_names and\
               self.method_set == o.method_set

    def __hash__(self):
        return hash(self.__str__())

def GetGtClassInfoInstrumentedList(gt_methods_instrumented: Set[int],
                                   gt_class_info_list: List[ClassInfo]) -> List[ClassInfo]:
    '''
    Return a list of classes from the list of ground truth classes that
    were instrumented during dynamic analysis. The classes instrumented
    must have at least one method that was instrumented. Only methods that
    are instrumented are included in the new classes.
    '''
    gt_class_info_instrumented: List[ClassInfo] = list()

    for ci in gt_class_info_list:
        new_method_set: Set[MethodInfo] = set()
        for mi in ci.method_set:
            if mi.address in gt_methods_instrumented:
                new_method_set.add(mi)
        if new_method_set != []:
            instrumented_ci = copy.deepcopy(ci)
            instrumented_ci.method_set = new_method_set
            gt_class_info_instrumented.append(instrumented_ci)

    return gt_class_info_instrumented

def GetTypeSet(classes: List[ClassInfo], type_to_match: str) -> Set[MethodInfo]:
    '''
    Return set of all methods in the classes list that have the given type.
    '''
    type_set: Set[MethodInfo] = set()
    for class_info in classes:
        for method in class_info.method_set:
            if method.type == type_to_match:
                type_set.add(method)
    return type_set

def LoadAndRecordGtMethodStats(gt_methods_instrumented_path: str, ground_truth: List[ClassInfo]) -> Set[int]:
    '''
    Records statistics about dynamic analysis method, constructor, and destructor coverage.
    '''
    gt_methods_instrumented_set: Set[int] = set()

    with open(gt_methods_instrumented_path) as f:
        try:
            while True:
                addr = int(f.readline())
                gt_methods_instrumented_set.add(addr + kBaseAddr)
        except ValueError:
            pass

    ctor_set = GetTypeSet(ground_truth, "ctor")
    dtor_set = GetTypeSet(ground_truth, "dtor")

    ctor_instrumented = 0
    dtor_instrumented = 0

    for mi in ctor_set:
        if mi.address in gt_methods_instrumented_set:
            ctor_instrumented += 1

    for mi in dtor_set:
        if mi.address in gt_methods_instrumented_set:
            dtor_instrumented += 1

    gt_methods = 0
    for ci in ground_truth:
        for mi in ci.method_set:
            gt_methods += 1

    with open(gt_methods_instrumented_path + '.stats', 'w') as gt_method_info:
        gt_coverage_all_methods = float(len(gt_methods_instrumented_set)) / float(gt_methods)
        gt_coverage_ctor = float(ctor_instrumented) / float(len(ctor_set))
        gt_coverage_dtor = float(dtor_instrumented) / float(len(dtor_set))

        gt_method_info.write('all method coverage: {:.2}, ctor coverage: {:.2}, dtor coverage: {:.2}'
            .format(gt_coverage_all_methods, gt_coverage_ctor, gt_coverage_dtor))

        return gt_methods_instrumented_set

def LoadAndConvertJson(json_str: str) -> List[ClassInfo]:
    json = json5.load(open(json_str))

    class_info_list: List[ClassInfo] = list()

    for cls_k, cls_v in json['structures'].items():
        methods = cls_v['methods']
        members = cls_v['members']

        method_set: Set[MethodInfo] = set()
        parent_mangled_names: List[str] = list()

        # Search through class methods
        for method in methods.values():
            method_ea = int(method['ea'], 16)
            type = method['type']
            method_set.add(MethodInfo(method_ea, type))

        # Search through class members
        for member in members.values():
            is_member_parent = member['parent']
            if is_member_parent:
                parent_mangled_names.append(member['struc'])

        mangled_name = cls_k

        # print(parent_mangled_names)
        # print(ClassInfo(mangled_name, parent_mangled_names, method_set))

        class_info_list.append(ClassInfo(mangled_name, parent_mangled_names, method_set))

    return class_info_list

def ComputePrecision(true_positives: int, false_positives: int) -> float:
    if true_positives + false_positives == 0:
        return 0.0
    return float(true_positives) / float(true_positives + false_positives)

def ComputeRecall(true_positives: int, false_negatives: int) -> float:
    if true_positives + false_negatives == 0:
        return 0.0
    return float(true_positives) / float(true_positives + false_negatives)

def ComputeF1(precision: float, recall: float) -> float:
    if precision + recall == 0.0:
        return 0.0
    return (2.0 * precision * recall) / (precision + recall)

def FalseNegatives(gt: List[any], true_positives: int) -> int:
    return len(gt) - true_positives

def FalsePositives(gen_data: List[any], true_positives: int) -> int:
    return len(gen_data) - true_positives

def IntersectionSize(l1: List[any], l2: List[any]) -> int:
    size = 0
    for x in l1:
        if x in l2:
            size += 1
    return size

def NonemptyClasses(classes: List[ClassInfo]) -> List[ClassInfo]:
    '''
    @return The classes in the given data set that have a nonempty method set.
    '''
    return [x for x in classes if len(x.method_set) != 0]

def PrecisionAndRecallClasses(ground_truth: List[ClassInfo], generated_data: List[ClassInfo]) -> tuple:
    '''
    Two classes are equal if the intersection of their method sets is not
    empty. Exclude classes without methods. Ground truth classes can't be
    double counted as a match for multiple generated classes.
    '''
    ground_truth_excluding_empty_cls: List[ClassInfo] = NonemptyClasses(ground_truth)
    generated_data_excluding_empty_cls: List[ClassInfo] = NonemptyClasses(generated_data)

    matched_classes = MatchGenToGtClasses(ground_truth, generated_data)
    true_positives = len(matched_classes)

    false_negatives = FalseNegatives(ground_truth_excluding_empty_cls, true_positives)
    false_positives = FalsePositives(generated_data_excluding_empty_cls, true_positives)

    return (ComputePrecision(true_positives, false_positives), ComputeRecall(true_positives, false_negatives))

def PrecisionAndRecallMethods(ground_truth: List[ClassInfo], generated_data: List[ClassInfo]):
    '''
    Methods are equal if their address is equal.
    '''
    def to_method_set(data: List[ClassInfo]):
        '''Collect all methods in a single set (their address only)'''
        method_set: Set[int] = set()
        for class_info in data:
            for method in class_info.method_set:
                method_set.add(method.address)
        return method_set

    ground_truth_methods: Set[int] = to_method_set(ground_truth)
    generated_data_methods: Set[int] = to_method_set(generated_data)

    true_positives = IntersectionSize(generated_data_methods, ground_truth_methods)

    false_negatives = FalseNegatives(ground_truth_methods, true_positives)
    false_positives = FalsePositives(generated_data_methods, true_positives)

    return (ComputePrecision(true_positives, false_positives), ComputeRecall(true_positives, false_negatives))

def PrecisionAndRecallSpecificType(ground_truth: List[ClassInfo], generated_data: List[ClassInfo], t: str) -> tuple:
    '''
    Compares the set of methods that specifically have the same type.
    '''
    ground_truth_type = GetTypeSet(ground_truth, t)
    generated_data_type = GetTypeSet(generated_data, t)

    true_positives = IntersectionSize(generated_data_type, ground_truth_type)

    false_negatives = FalseNegatives(ground_truth_type, true_positives)
    false_positives = FalsePositives(generated_data_type, true_positives)

    return (ComputePrecision(true_positives, false_positives), ComputeRecall(true_positives, false_negatives))

def PrecisionAndRecallConstructors(ground_truth: List[ClassInfo], generated_data: List[ClassInfo]) -> tuple:
    return PrecisionAndRecallSpecificType(ground_truth, generated_data, kConstructorType)

def PrecisionAndRecallDestructors(ground_truth: List[ClassInfo], generated_data: List[ClassInfo]) -> tuple:
    return PrecisionAndRecallSpecificType(ground_truth, generated_data, kDestructorType)

def PrecisionAndRecallMethodsAssignedCorrectClass(ground_truth: List[ClassInfo], generated_data: List[ClassInfo]) -> tuple:
    class EvaluationResults:
        def __init__(self, precision: float, recall: float, ground_truth_class_size: int):
            self.precision = precision
            self.recall = recall
            self.ground_truth_class_size = ground_truth_class_size

    class PrecisionRecallF1:
        def __init__(self, p: float, r: float, f: float, gt_class: ClassInfo):
            self.precision = p
            self.recall = r
            self.f1 = f
            self.gt_class = gt_class

    results: List[EvaluationResults] = list()

    for generated_class in generated_data:
        precision_recall_f1_scores: Set[PrecisionRecallF1] = set()

        # Insert 0 precision, recall, and F-score struct into
        # precision_recall_f1_scores in case none of the ground truth sets
        # have methods shared with the generated class
        precision_recall_f1_scores.add(PrecisionRecallF1(0, 0, 0, None))

        for gt_class in ground_truth:
            true_positives = 0

            for method in generated_class.method_set:
                if method in gt_class.method_set:
                    true_positives += 1

            if true_positives == 0:
                continue

            false_negatives = FalseNegatives(generated_class.method_set, true_positives)
            false_positives = FalsePositives(gt_class.method_set, true_positives)

            precision = ComputePrecision(true_positives, false_positives)
            recall = ComputeRecall(true_positives, false_negatives)
            f1 = ComputeF1(precision, recall)

            precision_recall_f1_scores.add(PrecisionRecallF1(precision, recall, f1, gt_class))

        highest_f1_it: PrecisionRecallF1 = None
        for scores in precision_recall_f1_scores:
            if highest_f1_it is None or scores.f1 > highest_f1_it.f1:
                highest_f1_it = scores

        if highest_f1_it == None:
            results.append(EvaluationResults(0, 0, 0))
        else:
            results.append(EvaluationResults(
                highest_f1_it.precision,
                highest_f1_it.recall,
                0 if highest_f1_it.gt_class is None else len(highest_f1_it.gt_class.method_set)))

    # Consume results
    precision = 0
    recall = 0
    total_methods = 0
    for result in results:
        precision += result.precision * result.ground_truth_class_size
        recall += result.recall * result.ground_truth_class_size
        total_methods += result.ground_truth_class_size

    return (precision / (float(total_methods)), recall / float(total_methods))

def PrecisionAndRecallClassGraphAncestors(ground_truth: List[ClassInfo], generated_data: List[ClassInfo]) -> tuple:
    matched_classes = MatchGenToGtClasses(ground_truth, generated_data)

    def get_gen_gt_cls_pair(gen_cls_mangled_name: str) -> Tuple[ClassInfo, ClassInfo]:
        for gen_gt_cls_pair in matched_classes:
            if gen_gt_cls_pair[0].mangled_name == gen_cls_mangled_name:
                return gen_gt_cls_pair
        return None

    def get_gen_cls(gen_cls_mangled_name: str) -> ClassInfo:
        for cls in generated_data:
            if cls.mangled_name == gen_cls_mangled_name:
                return cls
        return None

    def get_gt_cls(gt_cls_mangled_name: str) -> ClassInfo:
        for cls in ground_truth:
            if cls.mangled_name == gt_cls_mangled_name:
                return cls
        return None

    true_positives = 0
    gt_size = 0
    gen_size = 0

    for gen_cls, gt_cls in matched_classes:
        if len(gen_cls.parent_mangled_names) == 0 and len(gt_cls.parent_mangled_names) == 0:
            # Both gen and ground truth share the "root" i.e. there are no
            # inheritance relationships and that has been correctly identified.
            true_positives += 1
            gen_size += 1
            gt_size += 1
        else:
            def find_ancestors(cls):
                ancestors: Set[ClassInfo] = set()

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

            gen_ancestors = find_ancestors(gen_cls)

            # Find all gt parent classes for the given gt class
            gt_parents: Set[ClassInfo] = set()
            for cls_name in gt_cls.parent_mangled_names:
                parent_gt_cls = get_gt_cls(cls_name)
                if parent_gt_cls != None:
                    gt_parents.add(parent_gt_cls)

            # Check if any of the ancestors match the gt
            for gen_ancestor in gen_ancestors:
                # Find the ground truth class associated with the ancestor.
                gen_gt_pair = get_gen_gt_cls_pair(gen_ancestor.mangled_name)

                if gen_gt_pair is not None and gen_gt_pair[1] in gt_parents:
                    true_positives += 1

            gen_size += len(gen_ancestors)
            gt_size += len(gt_parents)

    false_negatives = gt_size - true_positives
    false_positives = gen_size - true_positives

    return (ComputePrecision(true_positives, false_positives), ComputeRecall(true_positives, false_negatives))

    return 0, 0

def PrecisionAndRecallClassGraphEdges(ground_truth: List[ClassInfo], generated_data: List[ClassInfo]) -> tuple:
    matched_classes = MatchGenToGtClasses(ground_truth, generated_data)

    # TODO this is inefficient and can be sped up but for now we aren't worried
    # about evaluation efficiency.
    def get_gen_gt_cls_pair(cls_mangled_name: str) -> tuple:
        for gen_cls, gt_cls in matched_classes:
            if gen_cls.mangled_name == cls_mangled_name:
                return (gen_cls, gt_cls)
        return None

    true_positives = 0
    gt_size = 0
    gen_size = 0

    for gen_cls, gt_cls in matched_classes:
        # Count number of parents that the two classes share. The "ground truth"
        # for this measure is the total number of inheritance relationships. A true
        # positive would be when the generated and ground truth class share the
        # same inheritance relationship

        # Note: we don't expect parent names to be the same - instead we expect the
        # paired classes to be the same.
        if len(gen_cls.parent_mangled_names) == 0 and len(gt_cls.parent_mangled_names) == 0:
            # Both gen and ground truth share the "root" i.e. there are no
            # inheritance relationships and that has been correctly identified.
            true_positives += 1

            gen_size += 1
            gt_size += 1
        else:
            for parent_name in gen_cls.parent_mangled_names:
                gen_gt_pair = get_gen_gt_cls_pair(parent_name)
                if gen_gt_pair != None:
                    # Find gt cls mangled name from gen_gt_pair in gt_cls.parent_mangled_names
                    for name in gt_cls.parent_mangled_names:
                        if name == gen_gt_pair[1].mangled_name:
                            true_positives += 1
                            break
                else:
                    print("failed to find parent by the name of " +
                          parent_name + " for child " + gen_cls.mangled_name +
                          " because no gt class matches this parent")

        gen_size += len(gen_cls.parent_mangled_names)
        gt_size += len(gt_cls.parent_mangled_names)

    false_negatives = gt_size - true_positives
    false_positives = gen_size - true_positives

    return (ComputePrecision(true_positives, false_positives), ComputeRecall(true_positives, false_negatives))

def MatchGenToGtClasses(ground_truth: List[ClassInfo], generated_data: List[ClassInfo]) -> Set[Tuple[ClassInfo, ClassInfo]]:
    ground_truth_excluding_empty_cls = NonemptyClasses(ground_truth)
    generated_data_excluding_empty_cls = NonemptyClasses(generated_data)

    matched_classes: Set[Tuple[ClassInfo, ClassInfo]] = set()

    gt_classes_referenced: Set[ClassInfo] = set()

    for gen_cls in generated_data_excluding_empty_cls:
        # Find ground truth classes that have method sets
        # intersecting with the current generated class.
        # For each ground truth class that does have a nonzero
        # method set intersection size, record the size.
        gen_gt_intersection_sizes: Dict[int, List[ClassInfo]] = dict()
        for gt_cls in ground_truth_excluding_empty_cls:
            s = IntersectionSize(gen_cls.method_set, gt_cls.method_set)
            if s != 0:
                if s not in gen_gt_intersection_sizes:
                    gen_gt_intersection_sizes[s] = list()
                gen_gt_intersection_sizes[s].append(gt_cls)

        # Get largest method set intersection that isn't already in the referenced
        # class set
        done = False
        for intersection_size in sorted(gen_gt_intersection_sizes.keys()):
            for gt_cls in gen_gt_intersection_sizes[intersection_size]:
                if gt_cls not in gt_classes_referenced:
                    gt_classes_referenced.add(gt_cls)
                    matched_classes.add((gen_cls, gt_cls))
                    done = True
                    break
            if done:
                break

    return matched_classes

def main():
    gt_class_info_list = LoadAndConvertJson(config['gtResultsJson'])

    gen_class_info_list = LoadAndConvertJson(config['resultsJson'])
    gt_methods_instrumented = LoadAndRecordGtMethodStats(config['gtMethodsInstrumentedPath'], gt_class_info_list)

    results_path = config['resultsPath']
    results_instrumented_path = config['resultsInstrumentedPath']

    def RunAllTests(gt_class_info_list: List[ClassInfo], file):
        file.write("evaluation criteria\tprecision\trecall\tf-score\n")

        def RunTest(name: str, test):
            try:
                precision, recall = test(gt_class_info_list, gen_class_info_list)
                f_score = ComputeF1(precision, recall)
                file.write('{}&{:.2f}&{:.2f}&{:.2f}\n'.format(name, precision, recall, f_score))
            except ZeroDivisionError as e:
                print(e)

        RunTest("Methods Assigned to Correct Class", PrecisionAndRecallMethodsAssignedCorrectClass)
        RunTest("Individual Classes", PrecisionAndRecallClasses)
        RunTest("Constructors", PrecisionAndRecallConstructors)
        RunTest("Destructors", PrecisionAndRecallDestructors)
        RunTest("Methods", PrecisionAndRecallMethods)
        RunTest("Class Graph Edges", PrecisionAndRecallClassGraphEdges)
        RunTest("Class Graph Ancestors", PrecisionAndRecallClassGraphAncestors)

    with open(results_path, 'w') as gt_out:
        RunAllTests(gt_class_info_list, gt_out)

    gt_class_info_instrumented_list = GetGtClassInfoInstrumentedList(gt_methods_instrumented, gt_class_info_list)

    with open(results_instrumented_path, 'w') as gt_out_instrumented:
        RunAllTests(gt_class_info_instrumented_list, gt_out_instrumented)

if __name__ == '__main__':
    main()
