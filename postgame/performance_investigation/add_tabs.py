import shutil
from pathlib import Path


def add_tabs(ot_file: Path):
    tab_file = ot_file.with_name(ot_file.name + "-tabs")

    tabs = ""

    already_tabbed = False

    with tab_file.open("w") as out_file:
        for line in ot_file.open():
            if "\t" in line:
                already_tabbed = True
                break

            split_lines = line.split()
            if len(split_lines) > 1 and split_lines[1] == "1":
                out_file.write(tabs + line)
                tabs += "\t"
            else:
                tabs = tabs[1:]
                out_file.write(tabs + line)

    if not already_tabbed:
        shutil.copy(tab_file, ot_file)

    tab_file.unlink()


def remove_tabs(ot_file: Path):
    tab_file = ot_file.with_name(ot_file.name + "-tabs")

    with tab_file.open("w") as out_file:
        for line in ot_file.open():
            line = line.strip()
            out_file.write(line + "\n")

    shutil.copy(tab_file, ot_file)
    tab_file.unlink()
