import json5
import sys
import os
import pathlib

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..'))

from parseconfig import config

kBaseAddr = 0x400000

def main():
    json_file = config['gtResultsJson']

    structures = json5.load(open(json_file, 'r'))['structures']

    method_addrs = set()

    for cls in structures.values():
        for method in cls['methods'].values():
            ea = method['ea']
            method_addrs.add(int(ea, 16) - kBaseAddr)

    with open(config['gtMethodsPath'], 'w') as f:
        for method in method_addrs:
            f.write(str(method) + '\n')

if __name__ == '__main__':
    main()
