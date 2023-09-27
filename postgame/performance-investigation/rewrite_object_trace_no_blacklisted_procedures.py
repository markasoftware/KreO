'''
Rewrites the object-trace file such that there are no blacklisted procedures
present. The postgame and other scripts filter out blacklisted methds; however,
it can be useful to remove blacklisted procedures from memory to save time and
memory when repeatedly performing analysis on the same object-trace.
'''

import pathlib
import sys
import os
import parse_object_trace

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..', '..'))

from parseconfig import parseconfig_argparse
from method_store import MethodStore

config = parseconfig_argparse()

def main():
    method_store = MethodStore()

    print('parsing object-traces...')
    _, object_traces = parse_object_trace.parseInput(config, method_store)

    print(f'found {len(object_traces)} traces')

    print('writing object-traces...')
    with open(config['objectTracesPath'], 'w') as f:
        for trace in object_traces:
            f.write(str(trace))
            f.write('\n\n')

if __name__ == '__main__':
    main()
