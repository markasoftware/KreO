set arguments=%1

@REM NOTE pregame.py **must** be run on Linux before this pipeline is run.

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
