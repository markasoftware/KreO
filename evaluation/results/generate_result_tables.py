import json
import os
from collections import defaultdict
from pathlib import Path

from evaluation.evaluation_data import EvaluationResult, EvaluationResults

SCRIPT_PATH = Path(__file__).parent

PRF_DESCRIPTOR = """(P indicates ``precision,'' R indicates ``recall,'' and F indicates ``F-Score.'')"""
ANALYSIS_TOOL_NAMES = ["Lego", "Lego+", "KreO", "OOAnalyzer"]
ANALYSIS_TOOL_KEYS = ["lego", "lego+", "kreo", "ooa"]

assert len(ANALYSIS_TOOL_NAMES) == len(ANALYSIS_TOOL_KEYS)


class PRF:
    def __init__(self, p: float, r: float, f: float):
        self.p = p
        self.r = r
        self.f = f

    def __str__(self):
        return f"{self.p} {self.r} {self.f}"


def sum_evaluation_results(data: list[EvaluationResult]) -> EvaluationResult:
    """
    Get average EvaluationResult given data, a list of EvaluationResult
    """
    avg = EvaluationResult(
        true_positives=sum(list(map(lambda x: x.true_positives, data))),
        false_positives=sum(list(map(lambda x: x.false_positives, data))),
        false_negatives=sum(list(map(lambda x: x.false_negatives, data))),
    )

    return avg


def evaluation_result_to_prf(data: EvaluationResult) -> PRF:
    return PRF(data.get_precision(), data.get_recall(), data.get_fscore())


def get_max_prf(data: list[EvaluationResult]) -> PRF:
    """
    Given a list of EvaluationResult, find the max precision, recall, and F-score and return it.
    """
    max_prf = PRF(0, 0, 0)
    for x in data:
        max_prf.p = max(max_prf.p, x.get_precision())
        max_prf.r = max(max_prf.r, x.get_recall())
        max_prf.f = max(max_prf.f, x.get_fscore())
    return max_prf


def GetPrfStr(data: EvaluationResult, max_prf: PRF | None = None):
    """
    Given EvaluationResult, return LaTeX string representation of the data, string
    contains precision, recall, F-Score. Elements bolded if they are equal
    to the max_prf.
    """
    if max_prf is None:
        max_prf = PRF(-1, -1, -1)

    def get_str_val(value: float, max_value: float):
        if abs(value - max_value) < 0.005:
            return "\\textbf{{{0:0.2f}}}".format(value)
        return "{0:0.2f}".format(value)

    p = get_str_val(data.get_precision(), max_prf.p)
    r = get_str_val(data.get_recall(), max_prf.r)
    f = get_str_val(data.get_fscore(), max_prf.f)
    return f"{p} & {r} & {f}"


def get_table_instrumented(
    analysis_tool: str,
    instrumented_results: dict[str, EvaluationResults],
):
    """Generates table given instrumented results"""

    def GetTableStart():
        nonlocal analysis_tool

        program = ""
        evaluation_category = ""
        tabular = ""

        def add_evaluation_category(name: str, last: bool = False):
            nonlocal evaluation_category
            nonlocal program
            nonlocal tabular

            evaluation_category += r" & \multicolumn{3}{|c}{\begin{tabular}{@{}c@{}}"
            evaluation_category += name
            evaluation_category += r"\end{tabular}}"

            program += "& P & R & F "
            tabular += "|ccc"

        add_evaluation_category(r"Class Graph\\Edges")
        add_evaluation_category(r"Individual Classes")
        add_evaluation_category(r"Constructors")
        add_evaluation_category(r"Destructors")
        add_evaluation_category(r"Methods")
        add_evaluation_category(r"Methods Assigned\\to Correct Class")

        start = f"""
\\begin{{table*}}
  \caption{{Evaluation of {analysis_tool} on the Covered Ground Truth {PRF_DESCRIPTOR}}}
  \label{{tab:{analysis_tool}-cgt}}
  \\tiny
  \\begin{{tabular}}{{l{tabular}}}
    \\toprule
    Evaluation Category{evaluation_category} \\\\
    Program {program}\\\\
    \\midrule
"""

        return start

    out = ""
    sums: dict[str, list[EvaluationResult]] = defaultdict(list)

    for evaluated_project, results in sorted(
        instrumented_results.items(), key=lambda x: x[0].lower()
    ):
        class_graph_edges = results.result_mapping["Class Graph Edges"]
        individual_classes = results.result_mapping["Individual Classes"]
        constructors = results.result_mapping["Constructors"]
        destructors = results.result_mapping["Destructors"]
        methods = results.result_mapping["Methods"]
        methods_assigned_to_correct_class = results.result_mapping[
            "Methods Assigned to Correct Class"
        ]

        out += f"    {evaluated_project} & "
        out += GetPrfStr(class_graph_edges) + " & "
        out += GetPrfStr(individual_classes) + " & "
        out += GetPrfStr(constructors) + " & "
        out += GetPrfStr(destructors) + " & "
        out += GetPrfStr(methods) + " & "
        out += GetPrfStr(methods_assigned_to_correct_class)
        out += "\\\\\n"

        sums["Class Graph Edges"].append(results.result_mapping["Class Graph Edges"])
        sums["Individual Classes"].append(results.result_mapping["Individual Classes"])
        sums["Constructors"].append(results.result_mapping["Constructors"])
        sums["Destructors"].append(results.result_mapping["Destructors"])
        sums["Methods"].append(results.result_mapping["Methods"])
        sums["Methods Assigned to Correct Class"].append(
            results.result_mapping["Methods Assigned to Correct Class"]
        )

    out += "    \\midrule\n"
    out += "    Average & "
    out += GetPrfStr(sum_evaluation_results(sums["Class Graph Edges"])) + " & "
    out += GetPrfStr(sum_evaluation_results(sums["Individual Classes"])) + " & "
    out += GetPrfStr(sum_evaluation_results(sums["Constructors"])) + " & "
    out += GetPrfStr(sum_evaluation_results(sums["Destructors"])) + " & "
    out += GetPrfStr(sum_evaluation_results(sums["Methods"])) + " & "
    out += GetPrfStr(sum_evaluation_results(sums["Methods Assigned to Correct Class"]))
    out += "\\\\\n"

    TABLE_END = r"""    \bottomrule
  \end{tabular}
\end{table*}
"""

    return GetTableStart() + out + TABLE_END


def get_full_table(caption: str, results: dict[str, dict[str, EvaluationResult]]):
    """Generate table with all results (not averaged).

    `results` maps evaluated project to a dict that maps the analysis tool to an
    EvaluationResult model.
    """

    def get_table_start(floating: bool = False):
        nonlocal caption

        label = "-".join(caption.split(" "))
        floating_str = "[H]" if floating else ""

        tools = ""
        project = ""
        tabular = ""

        def AddEvaluationTool(tool: str):
            nonlocal tools
            nonlocal project
            nonlocal tabular

            tools += f" & \multicolumn{{3}}{{|c}}{{{tool}}}"
            project += " & P & R & F"
            tabular += "|ccc"

        for tool in ANALYSIS_TOOL_NAMES:
            AddEvaluationTool(tool)

        return f"""
\\begin{{table}}{floating_str}
  \centering
  \caption{{Evaluation of Various Projects, {caption}}}
  \label{{tab:{label}}}
  \\begin{{tabular}}{{l{tabular}}}
    \\toprule
    Program{tools}\\\\
    Project{project}\\\\
    \midrule
"""

    TABLE_END = r"""    \bottomrule
  \end{tabular}
\end{table}"""

    result_list: dict[str, list[EvaluationResult]] = defaultdict(list)

    out = ""
    # sort by evaluated project name
    for evaluated_project, result in sorted(
        results.items(), key=lambda x: x[0].lower()
    ):
        max_prf = get_max_prf(list(result.values()))

        out += f"    {evaluated_project}"
        for tool_key in ANALYSIS_TOOL_KEYS:
            out += f" & {GetPrfStr(result[tool_key], max_prf)}"
            result_list[tool_key].append(result[tool_key])
        out += "\\\\\n"
    out += "    \\midrule\n"

    result_avg: dict[str, EvaluationResult] = dict()

    for tool in ANALYSIS_TOOL_KEYS:
        result_avg[tool] = sum_evaluation_results(result_list[tool])

    max_prf_avg = get_max_prf(list(result_avg.values()))

    out_avg = "    Average"
    for tool in ANALYSIS_TOOL_KEYS:
        out_avg += f" & {GetPrfStr(result_avg[tool], max_prf_avg)}"
    out_avg += "\\\\\n"

    out += out_avg

    return get_table_start(True) + out + TABLE_END


def get_average_table(results: dict[str, EvaluationResults]):
    """Generate table with all results, averaged"""

    out = ""
    # Maps evaluated project to dict that maps evaluation tool to a list of EvaluationResult
    sums: dict[str, dict[str, list[EvaluationResult]]] = defaultdict(
        lambda: defaultdict(list)
    )

    for evaluation_name, result in results.items():
        _, evaluation_tool = evaluation_name.split("-")
        for category in result.result_mapping:
            sums[category][evaluation_tool].append(result.result_mapping[category])

    for evaluation_type, result in sums.items():
        # Get average from result for each analysis tool
        result = dict(
            list(map(lambda x: (x[0], sum_evaluation_results(x[1])), result.items()))
        )
        max_prf = get_max_prf(list(result.values()))

        out += f"    {evaluation_type}"
        for tool in ANALYSIS_TOOL_KEYS:
            out += f"& {GetPrfStr(result[tool], max_prf)}"
        out += "\\\\\n"

    def GetTableStart():
        tabular = ""
        program = ""
        evaluation_category = ""
        for idx, tool in zip(range(len(ANALYSIS_TOOL_NAMES)), ANALYSIS_TOOL_NAMES):
            tabular += "|ccc"
            if idx + 1 == len(ANALYSIS_TOOL_NAMES):
                program += f" & \multicolumn{{3}}{{c}}{{{tool}}}"
            else:
                program += f" & \multicolumn{{3}}{{c|}}{{{tool}}}"
            evaluation_category += " & P & R & F"

        return f"""\\begin{{table*}}
\centering
\caption{{Evaluation of Various Projects, Average Results {PRF_DESCRIPTOR}}}
\label{{tab:averaged_results}}
\\begin{{tabular}}{{l{tabular}}}
    \\toprule
    Program{program}\\\\
    Evaluation Category{evaluation_category}\\\\
    \midrule
"""

    TABLE_END = r"""    \bottomrule
  \end{tabular}
\end{table*}"""

    return GetTableStart() + out + TABLE_END


def get_overall_results(results: dict[str, EvaluationResults]) -> str:
    """
    Averages PRF for all evaluation metrics for each tool
    """
    # map evaluation tool to list of evaluation results
    evaluation_tool_to_evaluation_results_map: dict[
        str, list[EvaluationResults]
    ] = defaultdict(list)

    for evaluation_name, result in results.items():
        _, evaluation_tool = evaluation_name.split("-")
        evaluation_tool_to_evaluation_results_map[evaluation_tool].append(result)

    evaluation_tool_to_sum_map: dict[str, list[EvaluationResult]] = {}

    # sum evaluation results for each evaluation category
    for (
        evaluation_tool,
        result_list,
    ) in evaluation_tool_to_evaluation_results_map.items():
        category_to_evaluation_result_map: dict[
            str, list[EvaluationResult]
        ] = defaultdict(list)

        for result in result_list:
            for category in result.result_mapping:
                category_to_evaluation_result_map[category].append(
                    result.result_mapping[category]
                )

        evaluation_tool_to_sum_map[evaluation_tool] = [
            sum_evaluation_results(x)
            for x in category_to_evaluation_result_map.values()
        ]

    def average_prf(ll: list[EvaluationResult]) -> PRF:
        p = sum(list(map(lambda x: x.get_precision(), ll)))
        r = sum(list(map(lambda x: x.get_recall(), ll)))
        f = sum(list(map(lambda x: x.get_fscore(), ll)))
        return PRF(p / len(ll), r / len(ll), f / len(ll))

    overall_avg: dict[str, PRF] = dict(
        [(k, average_prf(v)) for k, v in evaluation_tool_to_sum_map.items()]
    )

    out = ""

    def gen_avg_str(tool_key: str, tool_name: str):
        nonlocal overall_avg
        avg_prf = overall_avg[tool_key]
        return f"    {tool_name} & {avg_prf.p:0.2f} & {avg_prf.r:0.2f} & {avg_prf.f:0.2f}\\\\\n"

    for tool_key, tool_name in zip(ANALYSIS_TOOL_KEYS, ANALYSIS_TOOL_NAMES):
        out += gen_avg_str(tool_key, tool_name)

    TABLE_START = r"""\begin{table*}
  \centering
  \caption{Evaluation of Various Projects, Summarized Results}
  \label{tab:summarized_results}
  \begin{tabular}{l|c|c|c}
    \toprule
    Analysis Tool & Precision & Recall & F-Score\\
    \midrule
"""

    TABLE_END = r"""    \bottomrule
  \end{tabular}
\end{table*}"""

    return TABLE_START + out + TABLE_END


def collect_evaluation_result(
    evaluation_category: str,
    results: dict[str, EvaluationResults],
) -> dict[str, dict[str, EvaluationResult]]:
    out: dict[str, dict[str, EvaluationResult]] = defaultdict(dict)
    for name, result in results.items():
        evaluated_project, evaluation_tool = name.split("-")
        out[evaluated_project][evaluation_tool] = result.result_mapping[
            evaluation_category
        ]
    return out


def main():
    # Maps file name (without suffix) to EvaluationResults
    results: dict[str, EvaluationResults] = {}

    for analysis_result_file in (SCRIPT_PATH / "in").glob("*.json"):
        with analysis_result_file.open() as f:
            results[
                analysis_result_file.with_suffix("").name
            ] = EvaluationResults.model_validate_json(f.read())

    with (SCRIPT_PATH / "full-results.tex").open("w") as f:
        f.write(
            get_full_table(
                "Class Graph Edges",
                collect_evaluation_result("Class Graph Edges", results),
            )
            + "\n"
        )

        f.write(
            get_full_table(
                "Individual Classes",
                collect_evaluation_result("Individual Classes", results),
            )
            + "\n"
        )

        f.write(
            get_full_table(
                "Constructors",
                collect_evaluation_result("Constructors", results),
            )
            + "\n"
        )

        f.write(
            get_full_table(
                "Destructors",
                collect_evaluation_result("Destructors", results),
            )
            + "\n"
        )

        f.write(
            get_full_table(
                "Methods",
                collect_evaluation_result("Methods", results),
            )
            + "\n"
        )

        f.write(
            get_full_table(
                "Methods",
                collect_evaluation_result("Methods", results),
            )
            + "\n"
        )

        f.write(
            get_full_table(
                "Methods Assigned to Correct Class",
                collect_evaluation_result("Methods Assigned to Correct Class", results),
            )
            + "\n"
        )

    with (SCRIPT_PATH / "average-results.tex").open("w") as f:
        f.write(get_average_table(results))

    def get_instrumented_results(instrumented_results_fpath: Path):
        instrumented_results: dict[str, EvaluationResults] = {}

        for file in instrumented_results_fpath.glob("*.json"):
            with file.open() as f:
                instrumented_results[
                    file.with_suffix("").name
                ] = EvaluationResults.model_validate_json(f.read())

        return instrumented_results

    with (SCRIPT_PATH / "lego-instrumented-results.tex").open("w") as f:
        f.write(
            get_table_instrumented(
                "lego", get_instrumented_results(SCRIPT_PATH / "in-instrumented-lego")
            )
        )

    with (SCRIPT_PATH / "lego+-instrumented-results.tex").open("w") as f:
        f.write(
            get_table_instrumented(
                "lego+", get_instrumented_results(SCRIPT_PATH / "in-instrumented-lego+")
            )
        )

    with (SCRIPT_PATH / "kreo-instrumented-results.tex").open("w") as f:
        f.write(
            get_table_instrumented(
                "kreo", get_instrumented_results(SCRIPT_PATH / "in-instrumented-kreo")
            )
        )

    with (SCRIPT_PATH / "overall-results.tex").open("w") as f:
        f.write(get_overall_results(results))
