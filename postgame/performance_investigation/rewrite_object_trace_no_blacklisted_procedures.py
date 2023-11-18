"""
Rewrites the object-trace file such that there are no blacklisted procedures
present. The postgame and other scripts filter out blacklisted methds; however,
it can be useful to remove blacklisted procedures from memory to save time and
memory when repeatedly performing analysis on the same object-trace.
"""

import sys
from pathlib import Path

import parse_object_trace
from typer import Typer

SCRIPT_PATH = Path(__file__).parent.absolute()

sys.path.append(str(SCRIPT_PATH / ".."))
sys.path.append(str(SCRIPT_PATH / ".." / ".."))

from method_store import MethodStore  # noqa: E402

from parseconfig import parseconfig  # noqa: E402

APP = Typer()


@APP.command()
def main(config: Path):
    cfg = parseconfig(config)

    method_store = MethodStore()

    print("parsing object-traces...")
    _, object_traces = parse_object_trace.parseInput(cfg, method_store)

    print(f"found {len(object_traces)} traces")

    print("writing object-traces...")
    with cfg.object_traces_path.open() as f:
        for trace in object_traces:
            f.write(str(trace))
            f.write("\n\n")


if __name__ == "__main__":
    APP()
