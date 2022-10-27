# (insert project name)

This implements our hybrid dynamic-static technique to extract OOP features from compiled C++
binaries.

## Setup

Python half:

1. Ensure Python and development libraries are installed. You may prefer pypy for performance. On
   Debian, this would be `sudo apt install pypy3 pypy3-dev`.
2. Create a virtualenv inside of the project directory by running `virtualenv oop`. To use a
   specific python executable (such as pypy), specify it with the `--python-` option.

	If your operating system is running an older version of python, you may wish to use conda
    instead of virtualenv, because conda makes it easy to install custom python versions.
3. Activate the virtualenv with `source ./oop/bin/activate`.
4. Install dependencies by `pip3 install -r requirements.txt`.

C++/Pintool half:

1. Download a recent Pin version from [Intel's
   website](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-binary-instrumentation-tool-downloads.html).
   Most of our testing was on Pin 3.25.
2. Run `make PIN_ROOT=/path/to/pin/download` inside the `pintool` directory.

## Running

There are three steps:

1. Initial static analysis, where procedure boundaries and other preliminary information is collected.
2. Dynamic analysis, where the program is actually run.
3. Final static analysis, where un-executed code paths are analyzed using constant propagation. The raw output from the dynamic analysis step is also be processed.

Because there are three separate scripts, and not all are in the same language, configuration is controlled by a JSON file instead of command-line arguments. All three parts take as their only argument a path to the JSON configuration, which in turn contains paths to other intermediate files created by stages 1 and 2.

An example configuration file with commented documentation is available at `arguments.example.json`.

Now, for how to actually run the stages:
1. `python pregame.py config.json`
2. `PIN_ROOT=/path/to/downloaded/pin-folder python game.py config.json` (it's also possible to run pin directly, read `game.py` to learn how)
3. `python postgame.py config.json`

The final output will be placed at the `final-output` path specified in the configuration!
