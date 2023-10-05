'''
Generates formatted JSON containing procedures from the given list as a single class.
'''

import pathlib
import json
import sys
import os

from typing import List

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..', '..'))

from evaluation.evaluation import MethodInfo

def generateJsonSingleClass(method_list: List[MethodInfo], json_fname: str):
    methods = dict()

    # If there are no methods associated with the trie node there might not be any methods in the set
    for method in method_list:
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

    json_file = open(json_fname, 'w')
    json_file.write(json.JSONEncoder(indent=True).encode(final_json))
