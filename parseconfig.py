# NOTE: all paths specified in the config are relative
# to the base directory (specified by base_directory)
# except for base_directory, which is relative to the json file

import json
from enum import StrEnum, auto
from pathlib import Path
from typing import Any

from pydantic import BaseModel


class Isa(StrEnum):
    X86 = auto()
    X86_64 = auto()


class AnalysisTool(StrEnum):
    KREO = auto()
    LEGO = auto()
    LEGO_PLUS = auto()


class Config(BaseModel):
    config_fname: Path

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

    debug_function: int | None = None

    pin_root: Path | None = None

    isa: Isa = Isa.X86

    def model_post_init(self, __context: Any):
        # Set base_directory relative to config_path
        self.base_directory = self.config_fname.parent / self.base_directory

        # ensure it exists
        self.base_directory.mkdir(exist_ok=True)

        def path_rel_base(path: Path) -> Path:
            return self.base_directory / path

        self.method_candidates_path = path_rel_base(self.method_candidates_path)
        self.blacklisted_methods_path = path_rel_base(self.blacklisted_methods_path)
        self.gt_methods_path = path_rel_base(self.gt_methods_path)
        self.gt_methods_instrumented_path = path_rel_base(
            self.gt_methods_instrumented_path
        )
        self.base_offset_path = path_rel_base(self.base_offset_path)
        self.static_traces_path = path_rel_base(self.static_traces_path)
        self.object_traces_path = path_rel_base(self.object_traces_path)
        self.results_json = path_rel_base(self.results_json)
        self.dump_file = path_rel_base(self.dump_file)

        self.gt_results_json = path_rel_base(self.gt_results_json)
        self.pdb_file = path_rel_base(self.pdb_file)
        self.binary_path = path_rel_base(self.binary_path)
        self.results_path = path_rel_base(self.results_path)
        self.results_instrumented_path = path_rel_base(self.results_instrumented_path)

        super().model_post_init(__context)


def parseconfig(config_fname: Path, test_key: str) -> Config:
    cfg = Config(config_fname=config_fname, **json.load(config_fname.open())[test_key])

    return cfg
