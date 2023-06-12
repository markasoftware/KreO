@rem NOTE pregame.py **must** be run on Linux before this pipeline is run.

@echo off

set argCount=0
for %%x in (%*) do (
   set /A argCount+=1
   set "argVec[!argCount!]=%%~x"
)

if %argCount% NEQ 1 (
  echo "Expected exactly one argument -- the argument JSON file."
  @echo on
  exit /b 1
)

set arguments=%1

echo running dynamic analysis
python game.py %arguments%

if %errorlevel% NEQ 0 (
  echo "Previous command execution failed."
  @echo on
  exit /b %errorlevel%
)

echo running postgame
python postgame\postgame.py %arguments%

if %errorlevel% NEQ 0 (
  echo "Previous command execution failed."
  @echo on
  exit /b %errorlevel%
)

@echo on
