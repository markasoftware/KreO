import json
import os
import subprocess
import sys
from pathlib import Path

from typer import Typer

import evaluation.evaluation
import evaluation.extract_gt_methods
import evaluation.pdb_parser
from parseconfig import Config, Isa, parseconfig
from postgame.postgame import Postgame

APP = Typer()
SCRIPT_PATH = Path(__file__).parent.absolute()

cfg: Config | None = None


def bool_lowercase(b: bool):
    return "true" if b else "false"


@APP.command()
def pregame():
    assert cfg is not None

    executable_path = SCRIPT_PATH / "pregame" / "pregame"

    args: list[str] = [
        str(executable_path),
        "--enable-alias-analysis",
        bool_lowercase(cfg.enable_static_alias_analysis),
        "--enable-calling-convention-analysis",
        bool_lowercase(cfg.enable_calling_convention_analysis),
        "--enable-symbol-procedure-detection",
        bool_lowercase(cfg.enable_symbol_procedure_detection),
        "--method-candidates-path",
        str(cfg.method_candidates_path),
        "--static-traces-path",
        str(cfg.static_traces_path),
        "--base-offset-path",
        str(cfg.base_offset_path),
    ]

    if cfg.debug_function:
        args += ["--debug-function", str(cfg.debug_function)]

    args += ["--", str(cfg.binary_path)]

    process = subprocess.Popen(args)
    process.wait()


@APP.command()
def game():
    assert cfg is not None

    if cfg.pin_root:
        pin_root = cfg.pin_root
    elif "PIN_ROOT" in os.environ:
        pin_root = Path(os.environ["PIN_ROOT"])
    else:
        raise Exception(
            "PIN_ROOT environment variable or config option must be set to where you downloaded the pin kit."
        )

    # TODO: make it better at figuring out where the pintool is cross-platform!
    pin_executable_path = pin_root / "pin"

    pintool_shared_object = SCRIPT_PATH / "pintool"

    if cfg.isa == Isa.X86:
        pintool_shared_object = pintool_shared_object / "obj-ia32"
    elif cfg.isa == Isa.X86_64:
        pintool_shared_object = pintool_shared_object / "obj-intel64"
    else:
        msg = f"Unsupported ISA: {cfg.isa}"
        raise Exception(msg)

    if sys.platform == "linux" or sys.platform == "linux2" or sys.platform == "darwin":
        pintool_shared_object = pintool_shared_object / "Game.so"
    elif sys.platform == "win32":
        pintool_shared_object = pintool_shared_object / "Game.dll"
    else:
        msg = f"Unsupported operating system: {sys.platform}"
        raise Exception(msg)

    print(cfg.method_candidates_path.open().read())
    for line in cfg.method_candidates_path.open():
        print(line)

    process = subprocess.Popen(
        [
            str(pin_executable_path),
            "-t",
            str(pintool_shared_object),
            "-method-candidates",
            str(cfg.method_candidates_path),
            "-gt-methods",
            str(cfg.gt_methods_path),
            "-gt-methods-instrumented",
            str(cfg.gt_methods_instrumented_path),
            "-object-traces",
            str(cfg.object_traces_path),
            "-blacklisted-methods",
            str(cfg.blacklisted_methods_path),
            "--",
            str(cfg.binary_path),
        ]
    )

    process.wait()


@APP.command()
def postgame():
    assert cfg is not None

    Postgame(cfg).main()


@APP.command()
def demangle_all_names():
    assert cfg is not None

    demangled_method_candidates: dict[str, str] = dict()

    ot_name_map_path = cfg.object_traces_path.with_name(
        cfg.object_traces_path.name + "-name-map"
    )

    for line in ot_name_map_path.open():
        line = line.split(" ", 2)
        try:
            res = subprocess.run(["undname", line[1].strip()], capture_output=True)
            stdout = str(res.stdout, "UTF-8")
            undecorated = stdout.splitlines()[4].split(":-", 2)[1][2:-1]
            demangled_method_candidates[line[0].strip()] = undecorated
        except FileNotFoundError:
            print("undname failed")
            return

    with ot_name_map_path.open("w") as f:
        for addr, method in demangled_method_candidates.items():
            f.write(addr + " " + method + "\n")


@APP.command()
def eval():
    assert cfg is not None
    evaluation.evaluation.main(cfg)


@APP.command()
def independent_evaluation(
    results_json: Path,
    results_path: Path,
):
    assert cfg is not None
    evaluation.evaluation.run_evaluation(
        cfg.gt_results_json, results_json, results_path, None, None
    )


@APP.command()
def pdb_parser():
    assert cfg is not None
    evaluation.pdb_parser.main(cfg.dump_file, cfg.gt_results_json)


@APP.command()
def run_pipeline():
    assert cfg is not None
    game()
    postgame()


@APP.command()
def generate_dump():
    assert cfg is not None

    cvdump_exe = SCRIPT_PATH / "evaluation" / "cvdump.exe"

    pdb_to_dump = cfg.pdb_file

    with cfg.dump_file.open("wb") as outfile:
        _ = subprocess.check_call(
            [cvdump_exe, pdb_to_dump],
            shell=True,
            stdout=outfile,
            stderr=subprocess.STDOUT,
        )


@APP.command()
def extract_gt_methods():
    assert cfg is not None
    evaluation.extract_gt_methods.main(cfg)


@APP.command()
def run_pipeline_evaluation():
    assert cfg is not None
    generate_dump()
    extract_gt_methods()
    pdb_parser()
    game()
    postgame()
    eval()


@APP.command()
def run_pipeline_after_game():
    assert cfg is not None
    postgame()
    eval()


@APP.callback()
def main(config: Path, test: str):
    global cfg
    cfg = parseconfig(config, test)


if __name__ == "__main__":
    APP()
