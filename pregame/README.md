## Installation

To run pregame, you must install Rose on Ubuntu 20.04.  Follow instructions
[here](https://github.com/rose-compiler/rose/wiki/Install-Rose-From-Source) to
install Rose. When running `./configure`, be sure to specify
`--enable-languages=c,c++,binaries`. Also assuming you have boost installed
already, use the `--with-boost` flag, passing the location of boost. Note that
this is the directory where the files `./lib/libboost*` are installed. For me,
boost libraries are installed in `/usr/local/lib/`, so I pass `/usr/local/` to
`--with-boost=`. 

Note that currently the `Makefile` assumes you have specified
`--prefix=/usr/local` when running `./configure`. As such, the library path to
Rose libraries is hardcoded to `LD_LIBRARY_PATH=/usr/local/lib` since for me
Rose can't be found otherwise. Change `LD_LIBRARY_PATH` as required, or install
Rose in a location where libraries are default discovered.

## Running

Build via running `make all`. Run the pregame via `pregame.py` in the base directory.
