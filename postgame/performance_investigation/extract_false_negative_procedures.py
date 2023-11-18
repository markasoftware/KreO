"""
Extract procedures that are in the ground truth but not in the results json
(false negatives).
"""

import json
import os
import pathlib
import sys
from pathlib import Path

from generate_json_single_class import generateJsonSingleClass

import postgame.analysis_results as ar

SCRIPT_PATH = Path(__file__).parent.absolute()

sys.path.append(str(SCRIPT_PATH / ".." / ".."))

from evaluation.evaluation import ClassInfo, LoadClassInfoListFromJson, MethodInfo
from parseconfig import Config  # noqa: E402


def main(cfg: Config):
    gt_results = ar.AnalysisResults(**json.load(cfg.gt_results_json.open()))
    results = ar.AnalysisResults(**json.load(cfg.results_json.open()))

    gt_methods = gt_results.get_methods()
    methods = results.get_methods()

    # resultsAddrs = set(map(lambda x: x.address, methods))

    false_negative_methods = [
        meth for meth in gt_methods if meth.address not in resultsAddrs
    ]

    fname = cfg.results_json.with_name(
        cfg.results_json.with_suffix("").name + "_falseneg.json"
    )
    generateJsonSingleClass(falseNegativeMethods, fname)
``