'''
Extract procedures that are in the results json but not in the ground truth json
(false positives). Useful in identifying which procedures are found by the tool
but not in the ground truth.
'''

import pathlib
import sys
import os

from typing import List, Set
from generate_json_single_class import generateJsonSingleClass

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..'))

from parseconfig import config
from evaluation.evaluation import LoadClassInfoListFromJson, ClassInfo, MethodInfo

gtClassInfo = LoadClassInfoListFromJson(config['gtResultsJson'])
print(config['gtResultsJson'])
resultsClassInfo = LoadClassInfoListFromJson(config['resultsJson'])

# Compile list of methods, constructors, destructors

def GetAllMethods(clsInfoList: List[ClassInfo]) -> Set[MethodInfo]:
    methods: Set[MethodInfo] = set()
    for cls in clsInfoList:
        for method in cls.method_set:
            methods.add(method)
    return methods

gtMethods = GetAllMethods(gtClassInfo)
gtAddrs = set(map(lambda x: x.address, gtMethods))

resultsMethods = GetAllMethods(resultsClassInfo)

print(0x41f2e0 in gtAddrs)

falsePositiveMethods = [meth for meth in resultsMethods if meth.address not in gtAddrs]

# 0x41f2e0

fname = os.path.splitext(config['resultsJson'])[0] + '_falsepos.json'
generateJsonSingleClass(falsePositiveMethods, fname)
