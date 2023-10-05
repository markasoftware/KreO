"""
Generate .dump file from the pdb file specified in the json config file. Uses
cvdump.exe to generate the dump. The resulting dump file will be written to the
dumpFile specified in the config file.
"""

import subprocess
import sys
from pathlib import Path

from typer import Typer

SCRIPT_PATH = Path(__file__).parent.absolute()

sys.path.append(str(SCRIPT_PATH / ".."))

from parseconfig import parseconfig  # noqa: E402

APP = Typer()


@APP.command()
def main(config: Path):
    cfg = parseconfig(config)

    cvdump_exe = SCRIPT_PATH / "cvdump.exe"

    pdb_to_dump = cfg.pdb_file

    with cfg.dump_file.open("wb") as outfile:
        _ = subprocess.check_call(
            [cvdump_exe, pdb_to_dump],
            shell=True,
            stdout=outfile,
            stderr=subprocess.STDOUT,
        )


if __name__ == "__main__":
    APP()
