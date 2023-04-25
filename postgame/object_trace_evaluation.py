'''
Perform evaluation of the object-trace given ground truth data about the
object-trace. For instance, we would like to know which object-traces start
with constructors and end in destructors and which object-traces don't.

We want to know the following:
* Why are constructors being mis-identified
* Why are destructors being mis-identified
'''

import pathlib
import os
import sys
import parse_object_trace

from method_store import MethodStore
from object_trace import ObjectTrace
from typing import List, Tuple, Set, Dict

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..'))

from parseconfig import config
from evaluation.evaluation import LoadClassInfoListFromJson, ClassInfo

TP_HEAD = 0
FP_HEAD = 1
TP_FINGERPRINT = 2
FP_FINGERPRINT = 3
TP_METHOD = 4
FP_METHOD_AS_CONSTRUCTOR = 5
FP_METHOD_AS_DESTRUCTOR = 6
FP_METHOD = 7
NONMATCHING_CALL_RETURNS = 8
FP_PROCEDURES = 9
IN_TAIL_AND_FINGERPRINT = 10

def idToStr(id):
    if id == TP_HEAD:
        return ''
    elif id == FP_HEAD:
        return 'FP_H'
    elif id == TP_FINGERPRINT:
        return ''
    elif id == FP_FINGERPRINT:
        return 'FP_F'
    elif id == TP_METHOD:
        return ''
    elif id == FP_METHOD_AS_CONSTRUCTOR:
        return 'FP_METH_AS_CTOR'
    elif id == FP_METHOD_AS_DESTRUCTOR:
        return 'FP_METH_AS_DTOR'
    elif id == FP_METHOD:
        return 'FP_METH'
    elif id == NONMATCHING_CALL_RETURNS:
        return 'NONMATCHING_CR'
    elif id == FP_PROCEDURES:
        return 'FP_PROC'
    elif id == IN_TAIL_AND_FINGERPRINT:
        return 'IN_T_AND_F'
    assert False

class EvaluationInfo:
    def __init__(self):
        # Maps object trace index to a set of ids that are filled out 
        # during evaluation.
        self._eval_results: Dict[int, Set[int]] = dict()

    def setEvalIdx(self, idx, id):
        if idx not in self._eval_results:
            self._eval_results[idx] = set()
        self._eval_results[idx].add(id)

    def toStr(self, idx):
        if idx not in self._eval_results:
            return ''

        ret = list()
        idx_results = self._eval_results[idx]
        for id in idx_results:
            id_str = idToStr(id)
            if id_str != '':
                ret.append(id_str)
        return ' '.join(ret)

    def setHeadLen(self, len):
        self._head_len = len

    def setFingerprintLen(self, len):
        self._fingerprint_len = len

    def collect(self):
        '''
        Return following information in a tuple
        * Length of head
        * Length of fingerprint
        * Number of false positive head elements
        * Number of false positive fingerprint elements
        '''
        fp_head = len(self._eval_results[FP_HEAD]) if FP_HEAD in self._eval_results else 0
        fp_fingerprint = len(self._eval_results[FP_FINGERPRINT]) if FP_FINGERPRINT in self._eval_results else 0
        return self._head_len, self._fingerprint_len, fp_head, fp_fingerprint

def writeResults(config, object_traces_with_labels: List[Tuple[ObjectTrace, EvaluationInfo]]):
    with open(config['objectTracesPath'] + '-eval', 'w') as f:
        num_head_elements = 0
        num_fingerprint_elements = 0
        num_fp_head = 0
        num_fp_fingerprint = 0
        for trace, eval_info in object_traces_with_labels:
            data = eval_info.collect()

            num_head_elements += data[0]
            num_fingerprint_elements += data[1]
            num_fp_head += data[2]
            num_fp_fingerprint += data[3]

            per_fp_head = data[2] / data[0]
            per_fp_fingerprint = data[3] / data[1]

            f.write(f'{per_fp_head:.2f} {per_fp_fingerprint:.2f} {data[0]} {data[1]}\n')

            for idx, entry in enumerate(trace.traceEntries):
                f.write(f'{str(entry)} {eval_info.toStr(idx)}\n')
            f.write('\n')
        
        per_fp_head = 1 - num_fp_head / num_head_elements
        per_fp_fingerprint = 1 - num_fp_fingerprint / num_fingerprint_elements
        f.write(f'{per_fp_head:.2f} {per_fp_fingerprint:.2f} {num_head_elements} {num_fingerprint_elements}')

def collectGroundTruthData(config):
    gt_class_info_list: List[ClassInfo] = LoadClassInfoListFromJson(config['gtResultsJson'])

    # collect set of destructors and constructors in the ground truth
    gt_constructors: Set[int] = set()
    gt_destructors: Set[int] = set()
    gt_methods: Set[int] = set()

    for cls in gt_class_info_list:
        for method in cls.method_set:
            if method.type == 'dtor':
                gt_destructors.add(method.address)
            elif method.type == 'ctor':
                gt_constructors.add(method.address)
            elif method.type == 'meth':
                gt_methods.add(method.address)
            else:
                raise RuntimeError(f'Invalid method type {method.type}!')

    return gt_constructors, gt_destructors, gt_methods

def collectFalsePosResults(config):
    fname = os.path.splitext(config['resultsJson'])[0] + '_falsepos.json'
    falsepos_class_info: List[ClassInfo] = LoadClassInfoListFromJson(fname)
    assert len(falsepos_class_info) == 1
    return set(map(lambda x: x.address, falsepos_class_info[0].method_set))

def main():
    method_store = MethodStore()

    print('loading ground truth...')
    gt_constructors, gt_destructors, gt_methods = collectGroundTruthData(config)

    falsepos_procedures = collectFalsePosResults(config)

    print('loading object-traces...')
    baseOffset, object_traces = parse_object_trace.parseInput(config, method_store)

    print('evaluating object-traces...')
    object_traces_with_labels: List[Tuple[ObjectTrace, EvaluationInfo]] = list()

    for trace in object_traces:
        head_len = len(trace.head)
        fingerprint_len = len(trace.fingerprint)

        eval_info = EvaluationInfo()
        eval_info.setHeadLen(head_len)
        eval_info.setFingerprintLen(fingerprint_len)

        procedure_stack: List[Tuple[int, int]] = list()

        for idx, entry in enumerate(trace.traceEntries):
            if entry.isCall:
                # Save call idx
                procedure_stack.append((entry.method.address, idx))
            else:
                # Only label returns (since when returning we have all information about the method call).

                call_addr, call_idx = procedure_stack.pop()

                if call_addr != entry.method.address:
                    eval_info.setEvalIdx(idx, NONMATCHING_CALL_RETURNS)

                idx_in_head = call_idx < head_len

                idx_in_fingerprint = idx >= len(trace.traceEntries) - fingerprint_len

                method = entry.method

                offset_addr = baseOffset + method.address

                if idx_in_head:
                    if offset_addr in gt_constructors:
                        eval_info.setEvalIdx(idx, TP_HEAD)
                    else:
                        eval_info.setEvalIdx(idx, FP_HEAD)

                if idx_in_fingerprint:
                    if offset_addr in gt_destructors:
                        eval_info.setEvalIdx(idx, TP_FINGERPRINT)
                    else:
                        eval_info.setEvalIdx(idx, FP_FINGERPRINT)

                if not idx_in_head and not idx_in_fingerprint:
                    if offset_addr in gt_methods:
                        eval_info.setEvalIdx(idx, TP_METHOD)
                    elif offset_addr in gt_constructors:
                        eval_info.setEvalIdx(idx, FP_METHOD_AS_CONSTRUCTOR)
                    elif offset_addr in gt_destructors:
                        eval_info.setEvalIdx(idx, FP_METHOD_AS_DESTRUCTOR)
                    else:
                        eval_info.setEvalIdx(idx, FP_METHOD)

                if offset_addr in falsepos_procedures:
                    eval_info.setEvalIdx(idx, FP_PROCEDURES)

        object_traces_with_labels.append((trace, eval_info))

    print('writing results...')

    writeResults(config, object_traces_with_labels)

if __name__ == '__main__':
    main()
