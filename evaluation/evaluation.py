import json5
import sys
import os
import copy
import pathlib

from typing import List, Set, Dict, Tuple, Callable
from io import TextIOWrapper

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..'))

kConstructorType = 'ctor'
kDestructorType = 'dtor'
kBaseAddr = 0x400000

class MethodInfo:
    '''
    Object that contains an address and type associated with a particular method.
    '''
    def __init__(self, address: int, type: str, name: str):
        self.address = address
        self.type = type
        self.name = name

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
    def __init__(self,
                 mangled_name: str,
                 parent_mangled_names: List[str],
                 method_set: Set[MethodInfo]):
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

    @param gt_methods_instrumented Set of instrumented methods addresses.
    @param gt_class_info_list List of all class.
    @return New list of classes. Each class only contains methods in the
    gt_methods_instrumented set. The gt_class_info_list is left unmodified.
    '''
    gt_class_info_instrumented: List[ClassInfo] = list()

    for ci in gt_class_info_list:
        new_method_set: Set[MethodInfo] = set()
        for mi in ci.method_set:
            if mi.address in gt_methods_instrumented:
                new_method_set.add(mi)
        if len(new_method_set) > 0:
            instrumented_ci = copy.deepcopy(ci)
            instrumented_ci.method_set = new_method_set
            gt_class_info_instrumented.append(instrumented_ci)

    return gt_class_info_instrumented

def GetMethodSetOfGivenType(classes: List[ClassInfo], type_to_match: str) -> Set[MethodInfo]:
    '''
    @return A set of all methods in the classes list that have the given
    type. If type_to_match is None, all methods will be returned.
    '''
    methods_with_type: Set[MethodInfo] = set()
    for class_info in classes:
        for method in class_info.method_set:
            if type_to_match is None or method.type == type_to_match:
                methods_with_type.add(method)
    return methods_with_type

def get_gt_methods_instrumented_set(gt_methods_instrumented_path: str) -> Set[int]:
    '''
    @param gt_methods_instrumented_path Path to file containing a list
    of instrumented methods.
    @return Set of method addresses in the ground truth that were instrumented.
    '''
    gt_methods_instrumented_set: Set[int] = set()

    with open(gt_methods_instrumented_path) as f:
        try:
            while True:
                addr = int(f.readline())
                gt_methods_instrumented_set.add(addr + kBaseAddr)
        except ValueError:
            pass

    return gt_methods_instrumented_set

def LoadAndRecordGtMethodStats(gt_methods_instrumented_set: Set[int],
                               ground_truth: List[ClassInfo],
                               gt_method_stats_fname: str) -> None:
    '''
    Records statistics about the number of methods, constructors, and
    destructors.

    @param gt_methods_instrumented_set Set of method addresses that were
    instrumented by dynamic analysis.
    @param ground_truth List of class info objects in the ground truth.
    @param gt_method_stats_fname Name of file to write method stats to. 
    '''
    ctor_set = GetMethodSetOfGivenType(ground_truth, 'ctor')
    dtor_set = GetMethodSetOfGivenType(ground_truth, 'dtor')

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

    with open(gt_method_stats_fname, 'w') as gt_method_info:
        gt_coverage_all_methods = float(len(gt_methods_instrumented_set)) / float(gt_methods) if gt_methods != 0 else 0
        gt_coverage_ctor = float(ctor_instrumented) / float(len(ctor_set)) if len(ctor_set) != 0 else 0.0
        gt_coverage_dtor = float(dtor_instrumented) / float(len(dtor_set)) if len(dtor_set) != 0 else 0.0

        gt_method_info.write('all method coverage: {:.2}, ctor coverage: {:.2}, dtor coverage: {:.2}'
            .format(gt_coverage_all_methods, gt_coverage_ctor, gt_coverage_dtor))

        return gt_methods_instrumented_set

def LoadClassInfoListFromJson(json_str: str) -> List[ClassInfo]:
    '''
    Load list of classes from the given JSON string.

    @param json_str JSON file to load in class data from.
    @return list of classes.
    '''
    json = json5.load(open(json_str))

    class_info_list: List[ClassInfo] = list()

    for mangled_name, cls_info in json['structures'].items():
        methods = cls_info['methods']
        members = cls_info['members']

        method_set: Set[MethodInfo] = set()
        parent_mangled_names: List[str] = list()

        # Extract class methods
        for method in methods.values():
            method_ea = int(method['ea'], 16)
            type = method['type']
            name = method['name']
            method_set.add(MethodInfo(method_ea, type, name))

        # Extract class members (parent classes)
        for member in members.values():
            is_member_parent = member['parent']
            if is_member_parent:
                parent_mangled_names.append(member['struc'])

        class_info_list.append(ClassInfo(mangled_name, parent_mangled_names, method_set))

    return class_info_list

# Helper methods provided for ease of readability.

def FalseNegatives(gt: List[any], true_positives: int) -> int:
    return len(gt) - true_positives

def FalsePositives(gen_data: List[any], true_positives: int) -> int:
    return len(gen_data) - true_positives

def IntersectionSize(l1: Set[any], l2: Set[any]) -> int:
    '''
    @return the length of set that contains the intersection of the two sets.
    '''
    return len(l1.intersection(l2))

def NonemptyClasses(classes: List[ClassInfo]) -> List[ClassInfo]:
    '''
    @return The classes in the given data set that have a nonempty method set.
    '''
    return [x for x in classes if len(x.method_set) != 0]

def PrecisionAndRecallClasses(ground_truth: List[ClassInfo],
                              generated_data: List[ClassInfo]) -> Tuple[int, int, int]:
    '''
    Computes and returns the precision and recall, comparing the ground
    truth and generated classes.

    Two classes are equal if the intersection of their method sets is not
    empty. Exclude classes without methods. Ground truth classes can't be
    double counted as a match for multiple generated classes.
    '''
    ground_truth_excluding_empty: List[ClassInfo] = NonemptyClasses(ground_truth)
    generated_data_excluding_empty: List[ClassInfo] = NonemptyClasses(generated_data)

    matched_classes = MatchGenToGtClasses(ground_truth, generated_data)
    true_positives = len(matched_classes)

    false_negatives = FalseNegatives(ground_truth_excluding_empty, true_positives)
    false_positives = FalsePositives(generated_data_excluding_empty, true_positives)

    return true_positives, false_positives, false_negatives

def PrecisionAndRecallMethods(ground_truth: List[ClassInfo],
                              generated_data: List[ClassInfo]) -> Tuple[int, int, int]:
    '''
    Computes and returns precision and recall of the methods in the
    ground truth and generated data.

    Methods are equal if their address is equal.
    '''
    ground_truth_methods: Set[MethodInfo] = GetMethodSetOfGivenType(ground_truth, None)
    ground_truth_methods = set(map(lambda x: x.address, ground_truth_methods))

    generated_data_methods: Set[MethodInfo] = GetMethodSetOfGivenType(generated_data, None)
    generated_data_methods = set(map(lambda x: x.address, generated_data_methods))

    true_positives = IntersectionSize(generated_data_methods, ground_truth_methods)

    false_negatives = FalseNegatives(ground_truth_methods, true_positives)
    false_positives = FalsePositives(generated_data_methods, true_positives)

    return true_positives, false_positives, false_negatives

def PrecisionAndRecallSpecificType(ground_truth: List[ClassInfo],
                                   generated_data: List[ClassInfo], t: str) -> Tuple[int, int, int]:
    '''
    Compares the set of methods that have the same type (t),
    returning the precision and recall.
    '''
    ground_truth_type = GetMethodSetOfGivenType(ground_truth, t)
    ground_truth_type = set(map(lambda x: x.address, ground_truth_type))
    generated_data_type = GetMethodSetOfGivenType(generated_data, t)
    generated_data_type = set(map(lambda x: x.address, generated_data_type))

    true_positives = IntersectionSize(generated_data_type, ground_truth_type)

    false_negatives = FalseNegatives(ground_truth_type, true_positives)
    false_positives = FalsePositives(generated_data_type, true_positives)

    return true_positives, false_positives, false_negatives

def PrecisionAndRecallConstructors(ground_truth: List[ClassInfo],
                                   generated_data: List[ClassInfo]) -> Tuple[int, int, int]:
    return PrecisionAndRecallSpecificType(ground_truth, generated_data, kConstructorType)

def PrecisionAndRecallDestructors(ground_truth: List[ClassInfo],
                                  generated_data: List[ClassInfo]) -> Tuple[int, int, int]:
    return PrecisionAndRecallSpecificType(ground_truth, generated_data, kDestructorType)

def PrecisionAndRecallMethodsAssignedCorrectClass(ground_truth: List[ClassInfo],
                                                  generated_data: List[ClassInfo]) -> Tuple[int, int, int]:
    '''
    Computes the precision and recall, where for each generated class, compare
    the method set to all ground truth method sets. Compute the F-1 score for
    each ground truth method set (where a true positive is when ground truth and
    generated method sets have the same method) and take the precision and
    recall scores associated with the highest F-1 score as the precision and
    recall for the particular generated class. The final precision/recall is the
    weighted sum of the scores for each generated class (weighted by generated
    class method set size). 
    '''

    class EvaluationResults:
        '''
        Structure containing results associated with a particular generated class.
        '''
        def __init__(self, true_positives: int, false_positives: int, false_negatives: int):
            self.true_positives = true_positives
            self.false_positives = false_positives
            self.false_negatives = false_negatives

        def __str__(self):
            return 'tp: {} fp: {} fn: {}'.format(self.true_positives, self.false_positives, self.false_negatives)

    gen_to_gt_classes = MatchGenToGtClasses(ground_truth, generated_data)

    results: List[EvaluationResults] = list()

    for gen_cls, gt_cls in gen_to_gt_classes:
        gen_cls_addrs = set(map(lambda x: x.address, gen_cls.method_set))
        gt_cls_addrs = set(map(lambda x: x.address, gt_cls.method_set))

        true_positives = len(gen_cls_addrs.intersection(gt_cls_addrs))
        false_positives = len(gen_cls_addrs) - true_positives
        false_negatives = len(gt_cls_addrs) - true_positives

        results.append(EvaluationResults(true_positives, false_positives, false_negatives))

    return sum(map(lambda x: x.true_positives, results)), sum(map(lambda x: x.false_positives, results)), sum(map(lambda x: x.false_negatives, results))

def get_gen_gt_cls_pair(gen_cls_mangled_name: str,
                        matched_classes: Set[Tuple[ClassInfo, ClassInfo]]) -> Tuple[ClassInfo, ClassInfo]:
    for gen_gt_cls_pair in matched_classes:
        if gen_gt_cls_pair[0].mangled_name == gen_cls_mangled_name:
            return gen_gt_cls_pair
    return None

def PrecisionAndRecallClassGraphAncestors(ground_truth: List[ClassInfo],
                                          generated_data: List[ClassInfo]) -> Tuple[int, int, int]:
    '''
    Computes the precision and recall, comparing class ancestors in the class
    hierarchy tree. If an ancestor exists in the generated class hierarchy and
    a matching class exists as a parent class for the ground truth class, this
    is a true positive.
    '''
    matched_classes = MatchGenToGtClasses(ground_truth, generated_data)

    def get_cls_from_data(data: List[ClassInfo], mangled_name: str) -> ClassInfo:
        '''Get ClassInfo object with associated mangled name from data.
        Returns None if no classes in the data have the given mangled name.'''
        for cls in data:
            if cls.mangled_name == mangled_name:
                return cls
        return None

    def get_gen_cls(gen_cls_mangled_name: str) -> ClassInfo:
        '''Get ClassInfo object with associated mangled name from generated data.'''
        return get_cls_from_data(generated_data, gen_cls_mangled_name)

    def get_gt_cls(gt_cls_mangled_name: str) -> ClassInfo:
        '''Get ClassInfo object with associated mangled name from ground truth.'''
        return get_cls_from_data(ground_truth, gt_cls_mangled_name)

    true_positives = 0
    gen_size = 0
    gt_size = 0

    for gen_cls, gt_cls in matched_classes:
        if len(gen_cls.parent_mangled_names) == 0 and len(gt_cls.parent_mangled_names) == 0:
            # Both gen and ground truth share the "root" i.e. there are no
            # inheritance relationships and that has been correctly identified.
            true_positives += 1
            gen_size += 1
            gt_size += 1
        else:
            def find_ancestors_gen_cls(cls: ClassInfo) -> Set[ClassInfo]:
                '''
                Find the given class's ancestors. The cls should be a generated class.
                Return a set of ancestor ClassInfo objects.
                '''
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

            gen_ancestors = find_ancestors_gen_cls(gen_cls)

            # Find all gt parent classes for the given gt class
            gt_parents: Set[ClassInfo] = set()
            for cls_name in gt_cls.parent_mangled_names:
                parent_gt_cls = get_gt_cls(cls_name)
                if parent_gt_cls != None:
                    gt_parents.add(parent_gt_cls)

            # Check if any of the ancestors match the gt
            for gen_ancestor in gen_ancestors:
                # Find the ground truth class associated with the ancestor.
                gen_gt_pair = get_gen_gt_cls_pair(gen_ancestor.mangled_name, matched_classes)

                if gen_gt_pair is not None and gen_gt_pair[1] in gt_parents:
                    true_positives += 1

            gen_size += len(gen_ancestors)
            gt_size += len(gt_parents)

    false_negatives = gt_size - true_positives
    false_positives = gen_size - true_positives

    return true_positives, false_positives, false_negatives

def PrecisionAndRecallClassGraphEdges(ground_truth: List[ClassInfo],
                                      generated_data: List[ClassInfo]) -> Tuple[int, int]:
    '''
    Similar to PrecisionAndRecallClassGraphAncestors, except instead of
    comparing ancestors in the class graph, directly compare edges in the graph.
    A true positive is if the generated and ground truth class graphs have a
    matching edge between two of the same classes.
    '''
    matched_classes = MatchGenToGtClasses(ground_truth, generated_data)

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
                gen_gt_pair = get_gen_gt_cls_pair(parent_name, matched_classes)
                if gen_gt_pair != None:
                    # Find gt cls mangled name from gen_gt_pair in gt_cls.parent_mangled_names
                    for name in gt_cls.parent_mangled_names:
                        if name == gen_gt_pair[1].mangled_name:
                            true_positives += 1
                            break
                else:
                    # If gen_gt_pair is None, this indicates a failure to find
                    # the class associated with the parent in the ground truth
                    # data.
                    print(f'Failed to find parent in generated data called {parent_name} because no ground truth class associated with the parent class.')

        gen_size += len(gen_cls.parent_mangled_names)
        gt_size += len(gt_cls.parent_mangled_names)

    false_negatives = gt_size - true_positives
    false_positives = gen_size - true_positives

    return true_positives, false_positives, false_negatives

def MatchGenToGtClasses(ground_truth: List[ClassInfo],
                        generated_data: List[ClassInfo]) -> Set[Tuple[ClassInfo, ClassInfo]]:
    '''
    Helper method that attempts to match ground truth and generated classes.
    Method sets are used to determine which classes match. Two classes match if
    they share methods from their method sets.

    If multiple ground truth classes exist with methods in a single ground truth
    method set, the ground truth class with more matching methods in its method
    set is chosen as the matched class.
    '''
    ground_truth_excluding_empty = NonemptyClasses(ground_truth)
    generated_data_excluding_empty = NonemptyClasses(generated_data)

    matched_classes: Set[Tuple[ClassInfo, ClassInfo]] = set()

    gt_classes_referenced: Set[ClassInfo] = set()

    for gen_cls in generated_data_excluding_empty:
        # Find ground truth classes that have method sets
        # intersecting with the current generated class.
        # For each ground truth class that does have a nonzero
        # method set intersection size, record the size.
        gen_gt_intersection_sizes: Dict[int, List[ClassInfo]] = dict()
        for gt_cls in ground_truth_excluding_empty:
            if gt_cls in gt_classes_referenced:
                continue

            gen_cls_method_set = set(map(lambda x: x.address, gen_cls.method_set))
            gt_cls_method_set = set(map(lambda x: x.address, gt_cls.method_set))
            s = IntersectionSize(gen_cls_method_set, gt_cls_method_set)
            if s != 0:
                if s not in gen_gt_intersection_sizes:
                    gen_gt_intersection_sizes[s] = list()
                gen_gt_intersection_sizes[s].append(gt_cls)

        # Get largest method set intersection.
        intersection_sizes = list(sorted(gen_gt_intersection_sizes.keys(), reverse=True))
        if intersection_sizes != []:
            gt_cls = gen_gt_intersection_sizes[intersection_sizes[0]][0]
            gt_classes_referenced.add(gt_cls)
            matched_classes.add((gen_cls, gt_cls))

    return matched_classes

def run_evaluation(gt_class_info, gen_class_info, results_path, results_instrumented_path, gt_methods_instrumented_path):
    gt_class_info_list = LoadClassInfoListFromJson(gt_class_info)

    gen_class_info_list = LoadClassInfoListFromJson(gen_class_info)

    if gt_methods_instrumented_path is not None:
        gt_methods_instrumented = get_gt_methods_instrumented_set(gt_methods_instrumented_path)

        LoadAndRecordGtMethodStats(gt_methods_instrumented,
                                gt_class_info_list,
                                gt_methods_instrumented_path + '.stats')

    def RunAllTests(gt_class_info_list: List[ClassInfo], file: TextIOWrapper):
        '''
        Runs all tests and writes results to the given file.
        '''
        def RunTest(name: str,
                    test: Callable[[List[ClassInfo], List[ClassInfo]], Tuple[int, int, int]]):
            true_positives, false_positives, false_negatives = test(gt_class_info_list, gen_class_info_list)
            file.write('{}&{}&{}&{}\n'.format(name, true_positives, false_positives, false_negatives))

        RunTest('Methods Assigned to Correct Class', PrecisionAndRecallMethodsAssignedCorrectClass)
        RunTest('Individual Classes', PrecisionAndRecallClasses)
        RunTest('Constructors', PrecisionAndRecallConstructors)
        RunTest('Destructors', PrecisionAndRecallDestructors)
        RunTest('Methods', PrecisionAndRecallMethods)
        RunTest('Class Graph Edges', PrecisionAndRecallClassGraphEdges)
        RunTest('Class Graph Ancestors', PrecisionAndRecallClassGraphAncestors)

    with open(results_path, 'w') as gt_out:
        RunAllTests(gt_class_info_list, gt_out)

    if gt_methods_instrumented_path is not None:
        gt_class_info_instrumented_list = GetGtClassInfoInstrumentedList(gt_methods_instrumented, gt_class_info_list)

        with open(results_instrumented_path, 'w') as gt_out_instrumented:
            RunAllTests(gt_class_info_instrumented_list, gt_out_instrumented)

def main():
    from parseconfig import parseconfig_argparse

    config = parseconfig_argparse()

    run_evaluation(config['gtResultsJson'], config['resultsJson'],
                   config['resultsPath'], config['resultsInstrumentedPath'],
                    config['gtMethodsInstrumentedPath'])

if __name__ == '__main__':
    main()
