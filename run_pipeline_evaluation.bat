set arguments=%1

@REM NOTE pregame.py **must** be run on Linux before this pipeline is run.

@REM Generate dump file from pdb
python analysis\generate_dump.py %arguments%

IF %ERRORLEVEL% NEQ 0 (
  echo "Previous command execution failed."
  exit %ERRORLEVEL%
)

@REM Generate gt-results.json file from the dump file
.\analysis\build\Debug\analyze_pdb_dump.exe %arguments%

IF %ERRORLEVEL% NEQ 0 (
  echo "Previous command execution failed."
  exit %ERRORLEVEL%
)

@REM Extract ground truth methods from results json file
python analysis\extract_gt_methods.py %arguments%

IF %ERRORLEVEL% NEQ 0 (
  echo "Previous command execution failed."
  exit %ERRORLEVEL%
)

@REM Run dynamic analysis
python game.py %arguments%

IF %ERRORLEVEL% NEQ 0 (
  echo "Previous command execution failed."
  exit %ERRORLEVEL%
)

@REM Run postgame, processing object traces and generating results.json
python postgame\postgame.py %arguments%

IF %ERRORLEVEL% NEQ 0 (
  echo "Previous command execution failed."
  exit %ERRORLEVEL%
)

@REM Analyze results
python analysis\evaluation.py %arguments%
