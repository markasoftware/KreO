import json5
import sys

kBaseAddr = 0x400000

def main():
    if len(sys.argv) != 2:
        print('usage: python extract_gt_methods.py <path/to/json>')
        sys.exit(1)

    json_file = sys.argv[1]

    structures = json5.load(open(json_file, 'r'))['structures']

    method_addrs = set()

    for cls in structures.values():
        for method in cls['methods'].values():
            ea = method['ea']
            method_addrs.add(int(ea, 16))

    for method in method_addrs:
        print(method)

if __name__ == '__main__':
    main()
