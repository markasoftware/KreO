'''
List method stats for the given project (given a config file).
'''

import json5
import sys
import os
import pathlib

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..'))

from parseconfig import config

json_file = config['gtResultsJson']

structures = json5.load(open(json_file, 'r'))['structures']

methods = 0
classes = 0

for cls in structures.values():
    for method in cls['methods'].values():
        methods += 1

    classes += 1

with open(config['gtMethodsInstrumentedPath'] + '.stats', 'r') as project_stats:
    stats = project_stats.read()
    stats = stats.split(',')
    stats = [x.split(':') for x in stats]
    method_coverage = int(100 * float(stats[0][1]))
    ctor_coverage = int(100 * float(stats[1][1]))
    dtor_coverage = int(100 * float(stats[2][1]))

    print(f'{classes} & {methods} & ${method_coverage}\%$ & ${ctor_coverage}\%$ & ${dtor_coverage}\%$\\\\')
