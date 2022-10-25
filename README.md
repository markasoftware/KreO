# (insert project name)

This implements our hybrid dynamic-static technique to extract OOP features from compiled C++
binaries.

## Setup

Python half:

1. Ensure Python and development libraries are installed. You may prefer pypy for performance. On Debian, this would be `sudo apt install pypy3 pypy3-dev`.
2. Create a virtualenv inside of the project directory by running `virtualenv oop`. To use a specific python executable (such as pypy), specify it with the `--python-` option.

	If your operating system is running an older version of python, you may wish to use conda instead of virtualenv, because conda makes it easy to install custom python versions.
3. Activate the virtualenv with `source ./oop/bin/activate`.
4. Install dependencies by `pip3 install -r requirements.txt`.

C++/Pintool half:

TODO

## Running

There are three steps:

1. Initial static analysis, where procedure boundaries and other preliminary information is collected.
2. Dynamic analysis, where the program is actually run.
3. Final static analysis, where un-executed code paths are analyzed using constant propagation. The raw output from the dynamic analysis step is also be processed.

To perform initial static analysis, run `python3 pregame.py - /path/to/binary` (or pass `-h` to see
extra options). Next, run `./game`, which uses the output from the pregame to determine what to do.
The game can be run multiple times to collect additional data. Finally, run `python3 postgame.py` to
perform the final analysis. The results will be placed in a file named `results.json`.

All three parts can take the command-line argument `-h` to see extra configuration options (eg, to override where they should place their output and where input should be read in from the preceding stage).
