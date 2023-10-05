import shutil
import subprocess
from pathlib import Path


def get_demangled_name(mangled_name: str) -> str:
    try:
        p1 = subprocess.Popen(["undname", mangled_name], stdout=subprocess.PIPE)
        assert p1.stdout is not None
        demangled_name = str(p1.stdout.read())
        demangled_name = demangled_name.split("\\n")
        demangled_name = demangled_name[4]
        demangled_name = demangled_name[
            demangled_name.find('"') + 1 : demangled_name.rfind('"')
        ]
    except FileNotFoundError as _:
        demangled_name = mangled_name

    return demangled_name


def add_function_names(ot_file: Path):
    fn_file = ot_file.with_name(ot_file.name + "-functions")

    functions: dict[int, str] = {}

    for line in ot_file.with_name(ot_file.name + "-name-map").open():
        split_line = line.split(" ")
        functions[int(split_line[0], 16)] = get_demangled_name(split_line[1].strip())

    fn_names_added = False

    with fn_file.open("w") as out_file:
        for line in ot_file.open():
            line = line.strip("\r\n")
            if line == "":
                out_file.write("\n")
            else:
                split_lines = line.split(" ")

                if len(split_lines) == 3:
                    fn_names_added = True
                    break

                addr = int(split_lines[0], 16)
                fn = functions.get(addr, None)

                if fn and fn not in line:
                    out_file.write(line + " " + fn + "\n")
                else:
                    out_file.write(line + "\n")

    if not fn_names_added:
        shutil.copy(fn_file, ot_file)

    fn_file.unlink()


def remove_function_names(ot_file: Path):
    fn_file = ot_file.with_name(ot_file.name + "-functions")

    with fn_file.open("w") as out_file:
        for line in ot_file.open():
            line = line.strip()
            if line == "":
                out_file.write("\n")
            else:
                split_lines = line.split(" ")

                if len(split_lines) > 2 or (
                    len(split_lines) == 2 and split_lines[1] != "1"
                ):
                    out_file.write(
                        split_lines[0] + (" 1" if split_lines[1] == "1" else "") + "\n"
                    )
                else:
                    out_file.write(line + "\n")

    shutil.copy(fn_file, ot_file)
    fn_file.unlink()
