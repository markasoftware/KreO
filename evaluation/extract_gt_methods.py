'''
Extract ground truth methods from the json file specified in
the configuration file passed in as an argument to this script.
'''

import json5
import sys
import os
import pathlib

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..'))

from parseconfig import parseconfig_argparse

config = parseconfig_argparse()

baseAddr = int(open(config['baseOffsetPath'], 'r').readline(), 16)

json_file = config['gtResultsJson']

structures = json5.load(open(json_file, 'r'))['structures']

method_addrs = set()

for cls in structures.values():
    for method in cls['methods'].values():
        ea = method['ea']
        method_addrs.add(int(ea, 16) - baseAddr)

with open(config['gtMethodsPath'], 'w') as f:
    for method in method_addrs:
        f.write(hex(method)[2:] + '\n')
