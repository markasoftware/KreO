import os

from typing import List, Tuple, Dict
from collections import defaultdict

SCRIPT_PATH = os.path.split(os.path.realpath(__file__))[0]

class PRF:
    def __init__(self, p: float, r: float, f: float):
        self.p = p
        self.r = r
        self.f = f

    def __str__(self):
        return f'{self.p} {self.r} {self.f}'

class RawData:
    def __init__(self, tp: str='0', fp: str='0', fn: str='0'):
        self.tp = int(tp)
        self.fp = int(fp)
        self.fn = int(fn)

    def ComputePrecision(self) -> float:
        '''
        Calculate and return precision.
        '''
        if self.tp + self.fp == 0:
            return 0.0
        return float(self.tp) / float(self.tp + self.fp)

    def ComputeRecall(self) -> float:
        '''
        Calculate and return recall.
        '''
        if self.tp + self.fn == 0:
            return 0.0
        return float(self.tp) / float(self.tp + self.fn)

    def ComputeF(self):
        '''
        Calculate and return the F-1 score (combination of precision and recall).
        '''
        p = self.ComputePrecision()
        r = self.ComputeRecall()
        if p + r == 0.0:
            return 0.0
        return (2.0 * p * r) / (p + r)

    def __str__(self):
        return f'tp: {self.tp} fp: {self.fp} fn: {self.fn}'

def ParseLine(line: str) -> Tuple[str, RawData]:
    line = line.split('&')
    return line[0], RawData(line[1], line[2], line[3])

def SumRawData(data: List[RawData]) -> RawData:
    '''
    Get average RawData given data, a list of RawData
    '''
    avg = RawData()
    avg.tp = sum(list(map(lambda x: x.tp, data)))
    avg.fp = sum(list(map(lambda x: x.fp, data)))
    avg.fn = sum(list(map(lambda x: x.fn, data)))
    return avg

def RawDataToPRF(data: RawData) -> PRF:
    return PRF(data.ComputePrecision(), data.ComputeRecall(), data.ComputeF())

def GetMaxPrf(data: List[RawData]) -> PRF:
    '''
    Given a list of RawData, find the max precision, recall, and F-score and return it.
    '''
    max_prf = PRF(0, 0, 0)
    for x in data:
        max_prf.p = max(max_prf.p, x.ComputePrecision())
        max_prf.r = max(max_prf.r, x.ComputeRecall())
        max_prf.f = max(max_prf.f, x.ComputeF())
    return max_prf

def GetPrfStr(data: RawData, max_prf: PRF=None):
    '''
    Given RawData, return LaTeX string representation of the data, string
    contains precision, recall, F-Score. Elements bolded if they are equal
    to the max_prf.
    '''
    if max_prf == None:
        max_prf = PRF(-1, -1, -1)

    def get_str_val(value, max_value):
        if abs(value - max_value) < 0.005:
            return '\\textbf{{{0:0.2f}}}'.format(value)
        return '{0:0.2f}'.format(value)

    return f'{get_str_val(data.ComputePrecision(), max_prf.p)} & {get_str_val(data.ComputeRecall(), max_prf.r)} & {get_str_val(data.ComputeF(), max_prf.f)}'

def GetTableInstrumented(analysis_tool, instrumented_results: Dict[str, Dict[str, RawData]]):
    '''Generates table given instrumented results'''

    def GetTableStart():
        nonlocal analysis_tool

        start = f'''
\\begin{{table*}}
  \caption{{Evaluation of {analysis_tool} on the Covered Ground Truth (P indicates ``precision,'' R indicates ``recall,'' and F indicates ``F-Score.'')}}
  \label{{tab:{analysis_tool}-cgt}}
  \\begin{{tabular}}{{l|ccc|ccc|ccc|ccc|ccc|ccc|ccc}}
    \\toprule
    Evaluation Category'''
        program = '    Program '

        def AddEvaluationCategory(name):
            nonlocal start
            nonlocal program
            start += r' & \multicolumn{3}{c|}{\begin{tabular}{@{}c@{}}'
            start += name
            start += r'\end{tabular}}'

            program += '& P & R & F '

        AddEvaluationCategory(r'Class Graph\\Edges')
        AddEvaluationCategory(r'Class Graph\\Ancestors')
        AddEvaluationCategory(r'Individual Classes')
        AddEvaluationCategory(r'Constructors')
        AddEvaluationCategory(r'Destructors')
        AddEvaluationCategory(r'Methods')
        AddEvaluationCategory(r'Methods Assigned\\to Correct Class')

        program += r'\\'

        start += '\\\\\n' + program + '\n    \midrule\n'

        return start

    out = ''
    sums: Dict[str, List[RawData]] = defaultdict(list)

    for project, result in sorted(instrumented_results.items(), key=lambda x: x[0].lower()):
        class_graph_edges = result['Class Graph Edges']
        class_graph_ancestors = result['Class Graph Ancestors']
        individual_classes = result['Individual Classes']
        constructors = result['Constructors']
        destructors = result['Destructors']
        methods = result['Methods']
        methods_assigned_to_correct_class = result['Methods Assigned to Correct Class']

        out += f'    {project} & '
        out += GetPrfStr(class_graph_edges) + ' & '
        out += GetPrfStr(class_graph_ancestors) + ' & '
        out += GetPrfStr(individual_classes) + ' & '
        out += GetPrfStr(constructors) + ' & '
        out += GetPrfStr(destructors) + ' & '
        out += GetPrfStr(methods) + ' & '
        out += GetPrfStr(methods_assigned_to_correct_class)
        out += '\\\\\n'

        sums['Class Graph Edges'].append(result['Class Graph Edges'])
        sums['Class Graph Ancestors'].append(result['Class Graph Ancestors'])
        sums['Individual Classes'].append(result['Individual Classes'])
        sums['Constructors'].append(result['Constructors'])
        sums['Destructors'].append(result['Destructors'])
        sums['Methods'].append(result['Methods'])
        sums['Methods Assigned to Correct Class'].append(result['Methods Assigned to Correct Class'])

    out += '    \\midrule\n'
    out += '    Average & '
    out += GetPrfStr(SumRawData(sums['Class Graph Edges'])) + ' & '
    out += GetPrfStr(SumRawData(sums['Class Graph Ancestors'])) + ' & '
    out += GetPrfStr(SumRawData(sums['Individual Classes'])) + ' & '
    out += GetPrfStr(SumRawData(sums['Constructors'])) + ' & '
    out += GetPrfStr(SumRawData(sums['Destructors'])) + ' & '
    out += GetPrfStr(SumRawData(sums['Methods'])) + ' & '
    out += GetPrfStr(SumRawData(sums['Methods Assigned to Correct Class']))
    out += '\\\\\n'

    TABLE_END = r'''    \bottomrule
  \end{tabular}
\end{table*}
'''

    return GetTableStart() + out + TABLE_END

def GetFullTable(caption: str, results: Dict[str, Dict[str, RawData]]):
    '''Generate table with all results (not averaged).
    results maps evaluated project to a dict that maps the analysis tool to the results'''

    def GetTableStart(floating=False):
        nonlocal caption

        label = '-'.join(caption.split(' '))
        floating_str = '[H]' if floating else ''
        return f'''
\\begin{{table}}{floating_str}
  \centering
  \caption{{Evaluation of Various Projects, {caption}}}
  \label{{tab:{label}}}
  \\begin{{tabular}}{{l|ccc|ccc|ccc}}
    \\toprule
    Program & \multicolumn{{3}}{{c|}}{{Lego}} & \multicolumn{{3}}{{c|}}{{\projname}} & \multicolumn{3}{{c}}{{OOAnalyzer}}\\\\
    Project & Precision & Recall & F-Score & Precision & Recall & F-Score & Precision & Recall & F-Score\\\\
    \midrule
'''

    TABLE_END = r'''    \bottomrule
  \end{tabular}
\end{table}'''

    result_list: Dict[str, List[RawData]] = defaultdict(list)

    out = ''
    # sort by evaluated project name
    for evaluated_project, result in sorted(results.items(), key=lambda x: x[0].lower()):
        assert 'lego' in result
        assert 'kreo' in result
        assert 'ooa' in result

        lego_result = result['lego']
        kreo_result = result['kreo']
        ooa_result = result['ooa']

        max_prf = GetMaxPrf(result.values())

        out += f'    {evaluated_project} & {GetPrfStr(lego_result, max_prf)} & {GetPrfStr(kreo_result, max_prf)} & {GetPrfStr(ooa_result, max_prf)} \\\\\n'

        result_list['lego'].append(lego_result)
        result_list['kreo'].append(kreo_result)
        result_list['ooa'].append(ooa_result)

    out += '    \\midrule\n'

    result_avg = {
        'lego': SumRawData(result_list['lego']),
        'kreo': SumRawData(result_list['kreo']),
        'ooa': SumRawData(result_list['ooa'])
    }

    max_prf_avg = GetMaxPrf(result_avg.values())

    out_avg = f'    Average & {GetPrfStr(result_avg["lego"], max_prf_avg)} & {GetPrfStr(result_avg["kreo"], max_prf_avg)} & {GetPrfStr(result_avg["ooa"], max_prf_avg)} \\\\\n'

    out += out_avg

    return GetTableStart(True) + out + TABLE_END

def GetAverageTable(results: Dict[str, Dict[str, Dict[str, RawData]]]):
    '''Generate table with all results, averaged'''

    out = ''
    # Maps evaluation type to dict that maps analysis tool to a list of RawData
    sums: Dict[str, Dict[str, List[RawData]]] = defaultdict(lambda: defaultdict(list))

    for evaluation_type in results:
        for evaluated_project in results[evaluation_type]:
            for analysis_tool in results[evaluation_type][evaluated_project]:
                data = results[evaluation_type][evaluated_project][analysis_tool]
                sums[evaluation_type][analysis_tool].append(data)

    for evaluation_type, result in sums.items():
        # Get average from result for each analysis tool
        result = dict(list(map(lambda x: (x[0], SumRawData(x[1])), result.items())))
        max_prf = GetMaxPrf(result.values())
        out += f'    {evaluation_type} & {GetPrfStr(result["lego"], max_prf)} & {GetPrfStr(result["kreo"], max_prf)} & {GetPrfStr(result["ooa"], max_prf)} \\\\\n'

    TABLE_START = r'''\begin{table*}
  \centering
  \caption{Evaluation of Various Projects, Average Results}
  \label{tab:averaged_results}
  \begin{tabular}{l|ccc|ccc|ccc}
    \toprule
    Program & \multicolumn{3}{c|}{Lego} & \multicolumn{3}{c|}{\projname} & \multicolumn{3}{c}{OOAnalyzer}\\
    Evaluation Category & Precision & Recall & F-Score & Precision & Recall & F-Score & Precision & Recall & F-Score\\
    \midrule
'''

    TABLE_END = r'''    \bottomrule
  \end{tabular}
\end{table*}'''

    return TABLE_START + out + TABLE_END

def GetOverallResults(results: Dict[str, Dict[str, Dict[str, RawData]]]) -> str:
    '''
    Averages PRF for all evaluation metrics for each tool
    '''
    # maps tool to list of PRF scores for that tool
    overall: Dict[str, List[PRF]] = defaultdict(list)

    for evaluation_type in results:
        # Maps analysis tool to list of raw data, each data point belonging to a different project
        evaluation_sum: Dict[str, List[RawData]] = defaultdict(list)

        for project_name in results[evaluation_type]:
            for analysis_tool in results[evaluation_type][project_name]:
                data = results[evaluation_type][project_name][analysis_tool]
                evaluation_sum[analysis_tool].append(data)

        evaluation_sum_new: Dict[str, RawData] = dict([(x, SumRawData(evaluation_sum[x])) for x in evaluation_sum])
        for x in evaluation_sum_new:
            overall[x].append(evaluation_sum_new[x])

    def AveragePRF(l: List[RawData]) -> PRF:
        p = sum(list(map(lambda x: x.ComputePrecision(), l)))
        r = sum(list(map(lambda x: x.ComputeRecall(), l)))
        f = sum(list(map(lambda x: x.ComputeF(), l)))
        return PRF(p / len(l), r / len(l), f / len(l))

    overall_avg: Dict[str, PRF] = dict([(x, AveragePRF(overall[x])) for x in overall])

    out = ''

    def GenAvgStr(tool_key: str, tool_name: str):
        nonlocal overall_avg
        avg_prf = overall_avg[tool_key]
        return f'    {tool_name} & {avg_prf.p} & {avg_prf.r} & {avg_prf.f}\\\\\n'

    for tool_key, tool_name in zip(['lego', 'lego+', 'kreo', 'ooa'], ['Lego', 'Lego+', 'KreO', 'OOAnalyzer']):
        out += GenAvgStr(tool_key, tool_name)

    TABLE_START = r'''\begin{table*}
  \centering
  \caption{Evaluation of Various Projects, Summarized Results}
  \label{tab:summarized_results}
  \begin{tabular}{l|c|c|c}
    \toprule
    Analysis Tool & Precision & Recall & F-Score\\
    \midrule
'''

    TABLE_END = r'''    \bottomrule
  \end{tabular}
\end{table*}'''

    return TABLE_START + out + TABLE_END

def main():
    # Maps evaluation type to a dict. The inner dict maps evaluated project
    # name to another dict that maps evaluated project to another dict that
    # maps analysis tool to raw data
    results: Dict[str, Dict[str, Dict[str, RawData]]] = defaultdict(lambda: defaultdict(dict))

    for directory, _, files in os.walk(os.path.join(SCRIPT_PATH, 'in')):
        for file in files:
            if file == '.gitignore':
                continue

            filepath = os.path.join(directory, file)
            splitfilename = file.split('-')
            evaluated_project = splitfilename[0]
            analysis_tool = splitfilename[1]

            with open(filepath, 'r') as f:
                data = f.read().splitlines()
                data = data[1:]
                data: Dict[str, List[RawData]] = dict([ParseLine(x) for x in data])

                def map_to_results(evaluation_type: str):
                    results[evaluation_type][evaluated_project][analysis_tool] = data[evaluation_type]

                map_to_results('Class Graph Edges')
                map_to_results('Class Graph Ancestors')
                map_to_results('Individual Classes')
                map_to_results('Constructors')
                map_to_results('Destructors')
                map_to_results('Methods')
                map_to_results('Methods Assigned to Correct Class')

    with open('full-results.tex', 'w') as f:
        f.write(GetFullTable('Class Graph Edges', results['Class Graph Edges']) + '\n')
        f.write(GetFullTable('Class Graph Ancestors', results['Class Graph Ancestors']) + '\n')
        f.write(GetFullTable('Individual Classes', results['Individual Classes']) + '\n')
        f.write(GetFullTable('Constructors', results['Constructors']) + '\n')
        f.write(GetFullTable('Destructors', results['Destructors']) + '\n')
        f.write(GetFullTable('Methods', results['Methods']) + '\n')
        f.write(GetFullTable('Methods Assigned to Correct Class', results['Methods Assigned to Correct Class']) + '\n')

    with open('summarized-results.tex', 'w') as f:
        f.write(GetAverageTable(results))

    def GetInstrumentedResults(instrumented_results_fpath):
        instrumented_results: Dict[str, Dict[str, RawData]] = {}

        for directory, _, files in os.walk(instrumented_results_fpath):
            for evaluated_project in files:
                if evaluated_project == '.gitignore':
                    continue

                filepath = os.path.join(directory, evaluated_project)

                with open(filepath, 'r') as f:
                    data = f.read().splitlines()
                    data = data[1:]
                    data: Dict[str, List[RawData]] = dict([ParseLine(x) for x in data])
                    instrumented_results[evaluated_project] = data

        return instrumented_results

    with open('lego-instrumented-results.tex', 'w') as f:
        f.write(GetTableInstrumented('lego', GetInstrumentedResults('in-instrumented-lego')))

    with open('lego+-instrumented-results.tex', 'w') as f:
        f.write(GetTableInstrumented('lego+', GetInstrumentedResults('in-instrumented-lego+')))

    with open('kreo-instrumented-results.tex', 'w') as f:
        f.write(GetTableInstrumented('kreo', GetInstrumentedResults('in-instrumented-kreo')))

    with open('overall-results.tex', 'w') as f:
        f.write(GetOverallResults(results))

if __name__ == '__main__':
    main()
