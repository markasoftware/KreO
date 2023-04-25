'''
Extract procedures that are in the ground truth but not in the results json
(false negatives).
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
resultsClassInfo = LoadClassInfoListFromJson(config['resultsJson'])

# Compile list of methods, constructors, destructors

def GetAllMethods(clsInfoList: List[ClassInfo]) -> Set[MethodInfo]:
    methods: Set[MethodInfo] = set()
    for cls in clsInfoList:
        for method in cls.method_set:
            methods.add(method)
    return methods

gtMethods = GetAllMethods(gtClassInfo)

resultsMethods = GetAllMethods(resultsClassInfo)
resultsAddrs = set(map(lambda x: x.address, resultsMethods))

falseNegativeMethods = [meth for meth in gtMethods if meth.address not in resultsAddrs]

fname = os.path.splitext(config['resultsJson'])[0] + '_falseneg.json'
generateJsonSingleClass(falseNegativeMethods, fname)
