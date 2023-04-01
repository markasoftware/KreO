@rem NOTE pregame.py **must** be run on Linux before this pipeline is run.

set argCount=0
for %%x in (%*) do (
   set /A argCount+=1
   set "argVec[!argCount!]=%%~x"
)

if %argCount% NEQ 1 (
  echo "Expected exactly one argument -- the argument JSON file."
  exit /b 1
)

set arguments=%1

@rem Run dynamic analysis
python game.py %arguments%

if %errorlevel% NEQ 0 (
  echo "Previous command execution failed."
  exit /b %errorlevel%
)

@rem Run postgame, processing object traces and generating results.json
python postgame\postgame.py %arguments%

if %errorlevel% NEQ 0 (
  echo "Previous command execution failed."
  exit /b %errorlevel%
)
