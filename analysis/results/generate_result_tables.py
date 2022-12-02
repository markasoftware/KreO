import os

SCRIPT_PATH = os.path.split(os.path.realpath(__file__))[0]

def gen_table(caption, results):
    TABLE_START = f'''
\\begin{{table*}}
    \caption{{Evaluation of Various Projects, {caption}}}
  \label{{tab:class-graphs}}
  \\begin{{tabular}}{{l|ccc|ccc|ccc}}
    \\toprule
    Program & \multicolumn{{3}}{{c|}}{{Lego}} & \multicolumn{{3}}{{c|}}{{\projname}} & \multicolumn{3}{{c}}{{OOAnalyzer}}\\\\
    \midrule
'''

    TABLE_END = '''
  \\bottomrule
\end{tabular}
\end{table*}'''

    out = ''
    for project, result in results.items():
        lego_score = result['lego'] if 'lego' in result else ['X', 'X', 'X']
        kreo_score = result['kreo'] if 'kreo' in result else ['X', 'X', 'X']
        ooa_score = result['ooa'] if 'ooa' in result else ['X', 'X', 'X']

        out += f'{project} & {lego_score[0]} & {lego_score[1]} & {lego_score[2]} & {kreo_score[0]} & {kreo_score[1]} & {kreo_score[2]} & {ooa_score[0]} & {ooa_score[1]} & {ooa_score[2]} \\\\'

    return TABLE_START + out + TABLE_END

def main():
    results = {}

    print(os.path.join(SCRIPT_PATH, 'in'))
    for directory, _, files in os.walk(os.path.join(SCRIPT_PATH, 'in')):
        for file in files:
            filepath = os.path.join(directory, file)
            splitfilename = file.split('-')
            oss_project = splitfilename[0]
            tool = splitfilename[1]

            with open(filepath, 'r') as f:
                data = f.read().splitlines()
                data = data[1:]
                data = [d.split('&') for d in data]
                data = {d[0]: d[1:] for d in data}

                def map_to_results(name):
                    if name not in results:
                        results[name] = {}
                    if oss_project not in results[name]:
                        results[name][oss_project] = {}
                    results[name][oss_project][tool] = data[name]

                map_to_results('Class Graphs')
                map_to_results('Individual Classes')
                map_to_results('Constructors')
                map_to_results('Destructors')
                map_to_results('Methods')
                map_to_results('Methods Assigned to Correct Class')

    class_graphs = gen_table('Class Graphs', results['Class Graphs'])
    individual_classes = gen_table('Individual Classes', results['Individual Classes'])
    constructors = gen_table('Constructors', results['Constructors'])
    destructors = gen_table('Destructors', results['Destructors'])
    methods = gen_table('Methods', results['Methods'])
    methods_assigned_to_correct_class = gen_table('Methods Assigned to Correct Class', results['Methods Assigned to Correct Class'])

    print(class_graphs)
    print(individual_classes)
    print(constructors)
    print(destructors)
    print(methods)
    print(methods_assigned_to_correct_class)

if __name__ == '__main__':
    main()
