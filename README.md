# KreO

This implements our hybrid dynamic-static technique to extract OO features from compiled C++
binaries.

## Setup

This setup should work for both Linux and Windows. The Linux setup is simpler, but obviously you won't be able to run Windows executables.

### Python

1. Ensure Python and development libraries are installed. You may prefer pypy for performance. On
   Debian, this would be `sudo apt install pypy3 pypy3-dev`.
2. Create a virtualenv inside of the project directory by running `python -m venv .venv`. To use a
   specific python executable (such as pypy), use that as the python executable with which you create the venv.

	If your operating system is running an older version of python, you may wish to use conda
    instead of virtualenv, because conda makes it easy to install custom python versions.
3. Activate the virtualenv with `source ./.venv/bin/activate` (or, on Windows, run `.\.venv\Scripts\activate.bat`)
4. Install dependencies by `pip3 install -r requirements.txt`.

### Pintool (Linux)

1. Download and extract a recent Pin version from [Intel's
   website](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-binary-instrumentation-tool-downloads.html).
   Most of our testing was on Pin 3.25.
1. Set the `PIN_ROOT` environment variable to inside the extracted Pin directory. For example, `export PIN_ROOT=/path/to/download`.
2. Run `make` inside the `pintool` directory of KreO.

### Pintool (Windows)

TODO: We should eventually just distribute DLLs so people don't have to deal with Cygwin

1. Download a recent Pin version from [Intel's
   website](https://www.intel.com/content/www/us/en/developer/articles/tool/pin-a-binary-instrumentation-tool-downloads.html).
   Most of our testing was on Pin 3.25.
1. Install [Cygwin](https://www.cygwin.com). When selecting packages to install, make sure to add `make` in addition to the default selections.
1. Install a recent version of Microsoft® Visual© Studio™, which should include MSVC.
1. Open up the "x86 Native Tools Command Prompt for VS" or "x64 Native Tools Command Prompt for VS" depending on whether you want to build pin for 32-bit or 64-bit (you can do both one at a time and then have both versions available for later use).
1. Add the Cygwin binary directory to your path. Usually something like `set PATH=%PATH%;C:\cygwin64\bin`. Make sure to use MSVC linker instead of GNU linker utility.
1. Set the `PIN_ROOT` environment variable to inside the extract Pin directory, *using forward slashes*. For example, `set PIN_ROOT=C:/Users/Mark/pin`.
1. Run `make` from inside the `pintool` directory of KreO.

## Running

There are three steps:

1. Initial static analysis, where procedure boundaries and other preliminary information is collected.
2. Dynamic analysis, where the program is actually run.
3. Final static analysis, where un-executed code paths are analyzed using constant propagation. The raw output from the dynamic analysis step is also be processed.

Because there are three separate scripts, and not all are in the same language, configuration is controlled by a JSON file instead of command-line arguments. All three parts take as their only argument a path to the JSON configuration, which in turn contains paths to other intermediate files created by stages 1 and 2.

An example configuration file with commented documentation is available at `arguments.example.json`.

Now, for how to actually run the stages:
1. `./pregame.sh <path/to/binary>` (must be done on Linux)
1. `python pregame.py config.json`
2. `python game.py config.json` (make sure `PIN_ROOT` is set, as described in the setup above. You may need to edit your `.bashrc` file, or user environment variable settings on Windows, if you don't want to manually specify it every time you launch a terminal).
3. `python postgame.py config.json`

The final output will be placed at the `finalOutput` path specified in the configuration!

## Evaluation

To evaluate, before running KreO, run `./analysis/scripts/preanalysis.sh <path/to/pdb/dump/file> <path/to/output/json>`. Then after KreO runs, run `./analysis/scripts/postanalysis.sh <path/to/gt/json> <path/to/gen/json>`
