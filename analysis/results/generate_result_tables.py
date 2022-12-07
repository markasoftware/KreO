import os

SCRIPT_PATH = os.path.split(os.path.realpath(__file__))[0]

def gen_table_instrumented(instrumented_results):
    TABLE_START = '''
\\begin{table*}
  \caption{Evaluation of Lego on the Covered Ground Truth (P indicates ``precision,'' R indicates ``recall,'' and F indicates ``F-Score.''}
  \label{tab:lego-cgt}
  \\begin{tabular}{l|ccc|ccc|ccc|ccc|ccc|ccc}
    \\toprule
    Program & \multicolumn{3}{c|}{Class Graphs} & \multicolumn{3}{c|}{Individual Classes} & \multicolumn{3}{c|}{Constructors} & \multicolumn{3}{c|}{Destructors} & \multicolumn{3}{c|}{Methods} & \multicolumn{3}{c}{\\begin{tabular}{@{}c@{}}Methods Assigned to\\\\Correct Class\end{tabular}}\\\\
    & P & R & F & P & R & F & P & R & F & P & R & F & P & R & F & P & R & F \\\\
    \midrule
'''

    out = ''
    for project, result in instrumented_results.items():
        class_graphs = result['Class Graphs']
        individual_classes = result['Individual Classes']
        constructors = result['Constructors']
        destructors = result['Destructors']
        methods = result['Methods']
        methods_assigned_to_correct_class = result['Methods Assigned to Correct Class']

        out += f'    {project} & '
        out += f'{class_graphs[0]} & {class_graphs[1]} & {class_graphs[2]} &'
        out += f'{individual_classes[0]} & {individual_classes[1]} & {individual_classes[2]} &'
        out += f'{constructors[0]} & {constructors[1]} & {constructors[2]} &'
        out += f'{destructors[0]} & {destructors[1]} & {destructors[2]} &'
        out += f'{methods[0]} & {methods[1]} & {methods[2]} &'
        out += f'{methods_assigned_to_correct_class[0]} & {methods_assigned_to_correct_class[1]} & {methods_assigned_to_correct_class[2]}'
        out += '\\\\\n'

    TABLE_END = '''    \\bottomrule
  \end{tabular}
\end{table*}
'''

    return TABLE_START + out + TABLE_END

def gen_table(caption, results):
    label = '-'.join(caption.split(' '))
    TABLE_START = f'''
\\begin{{table*}}
    \caption{{Evaluation of Various Projects, {caption}}}
  \label{{tab:{label}}}
  \\begin{{tabular}}{{l|ccc|ccc|ccc}}
    \\toprule
    Program & \multicolumn{{3}}{{c|}}{{Lego}} & \multicolumn{{3}}{{c|}}{{\projname}} & \multicolumn{3}{{c}}{{OOAnalyzer}}\\\\
    & Precision & Recall & F-Score & Precision & Recall & F-Score & Precision & Recall & F-Score\\\\
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

    instrumented_results = {}    
    for directory, _, files in os.walk(os.path.join(SCRIPT_PATH, 'in-instrumented')):
        for file in files:
            filepath = os.path.join(directory, file)
            
            with open(filepath, 'r') as f:
                data = f.read().splitlines()
                data = data[1:]
                data = [d.split('&') for d in data]
                data = {d[0]: d[1:] for d in data}
                instrumented_results[file] = data

    instrumented = gen_table_instrumented(instrumented_results)

    print(instrumented)

if __name__ == '__main__':
    main()
