import os

SCRIPT_PATH = os.path.split(os.path.realpath(__file__))[0]

def convert_nested_str_list_to_float(l):
    def tuple_to_float(t):
        new_t = []
        for x in t:
            new_t.append(float(x))
        return new_t
    return list(map(lambda x: tuple_to_float(x), l))

def get_prf_average(l):
    def avg(l):
        return sum(l) / len(l)

    l = convert_nested_str_list_to_float(l)
    p = avg(list(map(lambda x: x[0], l)))
    r = avg(list(map(lambda x: x[1], l)))
    f = avg(list(map(lambda x: x[2], l)))
    return [p, r, f]

def get_max_prf(result):
    values = convert_nested_str_list_to_float(list(result.values()))
    maxes = [max(list(map(lambda x: x[i], values))) for i in range(3)]
    return maxes

def get_prf_str(prf, max_prf=None):
    def get_highlighted_str_value_if_max(value, max_value):
        if abs(value - max_value) < 0.005:
            return '\\textbf{{{0:0.2f}}}'.format(value)
        return '{0:0.2f}'.format(value)

    out = ''

    if max_prf is None:
        for value in prf:
            out += get_highlighted_str_value_if_max(float(value), -1) + ' & '
    else:
        for value, max in zip(prf, max_prf):
            out += get_highlighted_str_value_if_max(float(value), max) + ' & '
    return out[0:-3]

def gen_table_instrumented(instrumented_results):
    TABLE_START = '''
{\\footnotesize
\\begin{table*}
  \caption{Evaluation of Lego on the Covered Ground Truth (P indicates ``precision,'' R indicates ``recall,'' and F indicates ``F-Score.''}
  \label{tab:lego-cgt}
  \\begin{tabular}{l|ccc|ccc|ccc|ccc|ccc|ccc|ccc}
    \\toprule
    Program & \multicolumn{3}{c|}{\\begin{tabular}{@{}c@{}}Class Graph\\\\Edges\end{tabular}} & \multicolumn{3}{c|}{\\begin{tabular}{@{}c@{}}Class Graph\\\\Ancestors\end{tabular}} & \multicolumn{3}{c|}{Individual Classes} & \multicolumn{3}{c|}{Constructors} & \multicolumn{3}{c|}{Destructors} & \multicolumn{3}{c|}{Methods} & \multicolumn{3}{c}{\\begin{tabular}{@{}c@{}}Methods Assigned\\\\to Correct Class\end{tabular}}\\\\
    & P & R & F & P & R & F & P & R & F & P & R & F & P & R & F & P & R & F & P & R & F \\\\
    \midrule
'''

    out = ''
    sums = {}
    sums['Class Graph Edges'] = []
    sums['Class Graph Ancestors'] = []
    sums['Individual Classes'] = []
    sums['Constructors'] = []
    sums['Destructors'] = []
    sums['Methods'] = []
    sums['Methods Assigned to Correct Class'] = []

    for project, result in sorted(instrumented_results.items(), key=lambda x: x[0].lower()):
        class_graph_edges = result['Class Graph Edges']
        class_graph_ancestors = result['Class Graph Ancestors']
        individual_classes = result['Individual Classes']
        constructors = result['Constructors']
        destructors = result['Destructors']
        methods = result['Methods']
        methods_assigned_to_correct_class = result['Methods Assigned to Correct Class']

        out += f'    {project} & '
        out += get_prf_str(class_graph_edges) + ' & '
        out += get_prf_str(class_graph_ancestors) + ' & '
        out += get_prf_str(individual_classes) + ' & '
        out += get_prf_str(constructors) + ' & '
        out += get_prf_str(destructors) + ' & '
        out += get_prf_str(methods) + ' & '
        out += get_prf_str(methods_assigned_to_correct_class)
        out += '\\\\\n'

        sums['Class Graph Edges'].append(result['Class Graph Edges'])
        sums['Class Graph Ancestors'].append(result['Class Graph Ancestors'])
        sums['Individual Classes'].append(result['Individual Classes'])
        sums['Constructors'].append(result['Constructors'])
        sums['Destructors'].append(result['Destructors'])
        sums['Methods'].append(result['Methods'])
        sums['Methods Assigned to Correct Class'].append(result['Methods Assigned to Correct Class'])

    out += '    \\midrule\n'
    out += 'Average & '
    out += get_prf_str(get_prf_average(sums['Class Graph Edges'])) + ' & '
    out += get_prf_str(get_prf_average(sums['Class Graph Ancestors'])) + ' & '
    out += get_prf_str(get_prf_average(sums['Individual Classes'])) + ' & '
    out += get_prf_str(get_prf_average(sums['Constructors'])) + ' & '
    out += get_prf_str(get_prf_average(sums['Destructors'])) + ' & '
    out += get_prf_str(get_prf_average(sums['Methods'])) + ' & '
    out += get_prf_str(get_prf_average(sums['Methods Assigned to Correct Class']))
    out += '\\\\\n'

    TABLE_END = '''    \\bottomrule
  \end{tabular}
\end{table*}
}
'''

    return TABLE_START + out + TABLE_END

def gen_table(caption, results):
    label = '-'.join(caption.split(' '))
    def get_table_start(label, table_type, floating=False):
        floating_str = '[H]' if floating else ''
        return f'''
\\begin{{{table_type}}}{floating_str}
    \caption{{Evaluation of Various Projects, {caption}}}
  \label{{tab:{label}}}
  \\begin{{tabular}}{{l|ccc|ccc|ccc}}
    \\toprule
    Program & \multicolumn{{3}}{{c|}}{{Lego}} & \multicolumn{{3}}{{c|}}{{\projname}} & \multicolumn{3}{{c}}{{OOAnalyzer}}\\\\
    & Precision & Recall & F-Score & Precision & Recall & F-Score & Precision & Recall & F-Score\\\\
    \midrule
'''

    def get_table_end(table_type):
        return f'''\\bottomrule
\end{{tabular}}
\end{{{table_type}}}'''

    out_graph = {}
    out_graph['lego'] = list()
    out_graph['kreo'] = list()
    out_graph['ooa'] = list()

    out = ''
    for project, result in sorted(results.items(), key=lambda x: x[0].lower()):
        assert 'lego' in result
        assert 'kreo' in result
        assert 'ooa' in result

        max_prf = get_max_prf(result)

        out += f'{project} & {get_prf_str(result["lego"], max_prf)} & {get_prf_str(result["kreo"], max_prf)} & {get_prf_str(result["ooa"], max_prf)} \\\\\n'

        out_graph['lego'].append(result['lego'])
        out_graph['kreo'].append(result['kreo'])
        out_graph['ooa'].append(result['ooa'])

    out += '\\midrule\n'

    result_avg = {
        'lego': get_prf_average(out_graph['lego']),
        'kreo': get_prf_average(out_graph['kreo']),
        'ooa': get_prf_average(out_graph['ooa'])
    }
    max_prf_avg = get_max_prf(result_avg)

    out_avg = f'Average & {get_prf_str(result_avg["lego"], max_prf_avg)} & {get_prf_str(result_avg["kreo"], max_prf_avg)} & {get_prf_str(result_avg["ooa"], max_prf_avg)} \\\\\n'

    out += out_avg

    return ((get_table_start(label, 'table*') + out_avg + get_table_end('table*')), (get_table_start(label + '-2', 'table', True) + out + get_table_end('table')))

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

                map_to_results('Class Graph Edges')
                map_to_results('Class Graph Ancestors')
                map_to_results('Individual Classes')
                map_to_results('Constructors')
                map_to_results('Destructors')
                map_to_results('Methods')
                map_to_results('Methods Assigned to Correct Class')

    class_graph_edges_avg, class_graph_edges = gen_table('Class Graph Edges', results['Class Graph Edges'])
    class_graph_ancestors_avg, class_graph_ancestors = gen_table('Class Graph Ancestors', results['Class Graph Ancestors'])
    individual_classes_avg, individual_classes = gen_table('Individual Classes', results['Individual Classes'])
    constructors_avg, constructors = gen_table('Constructors', results['Constructors'])
    destructors_avg, destructors = gen_table('Destructors', results['Destructors'])
    methods_avg, methods = gen_table('Methods', results['Methods'])
    methods_assigned_to_correct_class_avg, methods_assigned_to_correct_class = gen_table('Methods Assigned to Correct Class', results['Methods Assigned to Correct Class'])

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

    with open('summarized-results.tex', 'w') as f:
        f.write(class_graph_edges_avg + '\n')
        f.write(class_graph_ancestors_avg + '\n')
        f.write(individual_classes_avg + '\n')
        f.write(constructors_avg + '\n')
        f.write(destructors_avg + '\n')
        f.write(methods_avg + '\n')
        f.write(methods_assigned_to_correct_class_avg + '\n')

    with open('full-results.tex', 'w') as f:
        f.write(class_graph_edges + '\n')
        f.write(class_graph_ancestors + '\n')
        f.write(individual_classes + '\n')
        f.write(constructors + '\n')
        f.write(destructors + '\n')
        f.write(methods + '\n')
        f.write(methods_assigned_to_correct_class + '\n')

    with open('instrumented-results.tex', 'w') as f:
        f.write(instrumented)

if __name__ == '__main__':
    main()
