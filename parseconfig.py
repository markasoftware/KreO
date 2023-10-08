# NOTE: all paths specified in the config are relative
# to the base directory (specified by base_directory)
# except for base_directory, which is relative to the json file

import json
from enum import StrEnum, auto
from pathlib import Path

from pydantic import BaseModel


class Isa(StrEnum):
    X86 = auto()
    X86_64 = auto()


class AnalysisTool(StrEnum):
    KREO = auto()
    LEGO = auto()
    LEGO_PLUS = auto()


class Config(BaseModel):
    analysis_tool: AnalysisTool

    cfg_mode: str = "fast"

    pdb_file: Path
    results_path: Path
    results_instrumented_path: Path
    base_directory: Path
    binary_path: Path

    method_candidates_path: Path = Path("method-candidates")
    blacklisted_methods_path: Path = Path("blacklisted-methods")
    gt_results_json: Path = Path("gt-results.json")
    gt_methods_path: Path = Path("gt-methods")
    gt_methods_instrumented_path: Path = Path("gt-methods-instrumented")
    base_offset_path: Path = Path("base-address")
    static_traces_path: Path = Path("static-traces")
    object_traces_path: Path = Path("object-traces")
    results_json: Path = Path("results.json")
    dump_file: Path = Path("project.dump")
    pdb_file: Path

    debug_function: int | None = None

    pin_root: Path | None = None

    isa: Isa = Isa.X86


def parseconfig(config_fname: Path) -> Config:
    cfg = Config(**json.load(config_fname.open()))

    # Set base_directory relative to config_path
    cfg.base_directory = config_fname.parent / cfg.base_directory

    # ensure it exists
    cfg.base_directory.mkdir(exist_ok=True)

    def path_rel_base(path: Path) -> Path:
        return cfg.base_directory / path

    cfg.method_candidates_path = path_rel_base(cfg.method_candidates_path)
    cfg.blacklisted_methods_path = path_rel_base(cfg.blacklisted_methods_path)
    cfg.gt_methods_path = path_rel_base(cfg.gt_methods_path)
    cfg.gt_methods_instrumented_path = path_rel_base(cfg.gt_methods_instrumented_path)
    cfg.base_offset_path = path_rel_base(cfg.base_offset_path)
    cfg.static_traces_path = path_rel_base(cfg.static_traces_path)
    cfg.object_traces_path = path_rel_base(cfg.object_traces_path)
    cfg.results_json = path_rel_base(cfg.results_json)
    cfg.dump_file = path_rel_base(cfg.dump_file)

    cfg.gt_results_json = path_rel_base(cfg.gt_results_json)
    cfg.pdb_file = path_rel_base(cfg.pdb_file)
    cfg.binary_path = path_rel_base(cfg.binary_path)
    cfg.results_path = path_rel_base(cfg.results_path)
    cfg.results_instrumented_path = path_rel_base(cfg.results_instrumented_path)

    return cfg
