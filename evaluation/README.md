## Usage

The analysis may be ran on Windows or Linux, but the expected usage is to run
analysis in `run_pipeline_evaluation.bat` on Windows. If running on Windows, simply `cd
build && cmake ..`, then open the project in Visual Studio and build it. This
will create the executable `analyze_pdb_dump.exe`. If running on Linux, you need
to modify `CMakeLists.txt`. Follow the instructions in `CMakeLists.txt`.
