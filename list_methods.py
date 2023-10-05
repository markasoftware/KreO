"""List methods from the given name map."""

from typer import Typer

APP = Typer()


@APP.command()
def main(name_map: str, addrs: list[str]):
    with open(name_map, "r") as f:
        for line in f.readlines():
            if line.split()[0] in addrs:
                print(line, end="")


if __name__ == "__main__":
    APP()
