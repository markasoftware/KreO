'''
Find method statistics for the given project config.
'''

import json5
import argparse
import os
import pathlib
import sys

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..'))

from parseconfig import parseconfig

class ProgramStatistics:
    def __init__(self, classes_with_methods, methods, subtyping_relations, method_coverage, constructor_coverage, destructor_coverage):
        self.classes_with_methods = classes_with_methods
        self.methods = methods
        self.subtyping_relations = subtyping_relations
        self.method_coverage = method_coverage
        self.constructor_coverage = constructor_coverage
        self.destructor_coverage = destructor_coverage

    def __str__(self):
        return f'${self.classes_with_methods}$ & ${self.methods}$ & ${self.subtyping_relations}$ & ${self.method_coverage}\%$ & ${self.constructor_coverage}\%$ & ${self.destructor_coverage}\%$'

def find_program_statistics(config):
    json_file = config['gtResultsJson']

    structures = json5.load(open(json_file, 'r'))['structures']

    methods = 0
    classes = 0
    subtyping_relationships = 0

    for cls in structures.values():
        for method in cls['methods'].values():
            methods += 1

        for superclass in cls['members'].values():
            subtyping_relationships += 1
            
        classes += 1

    with open(config['gtMethodsInstrumentedPath'] + '.stats', 'r') as project_stats:
        stats = project_stats.read()
        stats = stats.split(',')
        stats = [x.split(':') for x in stats]
        method_coverage = int(100 * float(stats[0][1]))
        ctor_coverage = int(100 * float(stats[1][1]))
        dtor_coverage = int(100 * float(stats[2][1]))

        return ProgramStatistics(classes, methods, subtyping_relationships, method_coverage, ctor_coverage, dtor_coverage)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('data_directory')
    args = parser.parse_args()

    def get_config_file(name):
        nonlocal args
        return os.path.join(args.data_directory, f'kreo-{name}.json')

    out = '''
\\begin{table*}[!t]
    \centering
    \caption{Subject Programs}
    \label{tab:program-metrics}
    \\begin{tabular}{l|c|c|c|c|c|c}
        \\toprule
        & Classes with & & Subtyping & \multicolumn{3}{c}{Coverage of program's unit test suite} \\\\ \cline{5-7}
        Program & methods & Methods & Relationships & Method & Constructor & Destructor \\\\
        \midrule
'''

    def get_stats_str(name):
        return f'        {name}\\cite{{{name}}} & {find_program_statistics(parseconfig(get_config_file(name)))} \\\\\n'

    out += get_stats_str('libbmp')
    out += get_stats_str('optparse')
    out += get_stats_str('ser4cpp')
    out += get_stats_str('tinyxml2')

    out += '''        \\bottomrule
    \end{tabular}
\end{table*}
'''

    print(out)

if __name__ == '__main__':
    main()
