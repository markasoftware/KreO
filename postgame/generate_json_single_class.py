'''
Generates formatted JSON containing procedures from the given list as a single class.
'''

import pathlib
import json
import sys
import os

from typing import List

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..'))

from evaluation.evaluation import MethodInfo

def generateJsonSingleClass(methodList: List[MethodInfo], jsonFname):
    methods = dict()
    # If there are no methods associated with the trie node there might not be any methods in the set
    for method in methodList:
        methods[hex(method.address)] = {
            'demangled_name': hex(method.address),
            'ea': hex(method.address),
            'import': False,
            'name': method.name,
            'type': method.type,
        }

    structures = dict()
    structures[''] = {
        'demangled_name': '',
        'name': '',
        'members': {},
        'methods': methods,
        'size': 0, 
        'vftables': [],
    }

    final_json = {
        'filename': '',
        'filemd5': '',
        'structures': structures,
        'vcalls': {},
        'version': '',
    }

    jsonFile = open(jsonFname, 'w')
    jsonFile.write(json.JSONEncoder(indent=True).encode(final_json))
