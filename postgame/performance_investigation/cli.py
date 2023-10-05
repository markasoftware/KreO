import sys
from pathlib import Path

from typer import Typer

SCRIPT_PATH = Path(__file__).parent.absolute()

sys.path.append(str(SCRIPT_PATH / ".." / ".."))

import postgame.performance_investigation.add_function_names as fns  # noqa: E402
import postgame.performance_investigation.add_tabs as tabs  # noqa: E402
import postgame.performance_investigation.remove_object_trace_bodies as remove_ot_bodies  # noqa: E402
from parseconfig import parseconfig  # noqa: E402

APP = Typer(rich_markup_mode="rich")


@APP.command()
def add_tabs(ot_file: Path):
    tabs.add_tabs(ot_file)


@APP.command()
def remove_tabs(ot_file: Path):
    tabs.remove_tabs(ot_file)


@APP.command()
def add_function_names(ot_file: Path):
    fns.add_function_names(ot_file)


@APP.command()
def remove_function_names(ot_file: Path):
    fns.remove_function_names(ot_file)


@APP.command()
def remove_object_trace_bodies(config_path: Path):
    remove_ot_bodies.remove_object_trace_bodies(parseconfig(config_path))


if __name__ == "__main__":
    APP()
