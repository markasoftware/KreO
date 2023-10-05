set argCount=0
for %%x in (%*) do (
   set /A argCount+=1
   set "argVec[!argCount!]=%%~x"
)

if %argCount% NEQ 1 (
  echo Expected exactly one argument -- the argument JSON file.
  @echo on
  exit /b 1
)

set arguments=%1

echo running postgame
python postgame\postgame.py %arguments%

if %errorlevel% NEQ 0 (
  echo Previous command execution failed.
  exit /b %errorlevel%
)

echo analyzing reuslts 
python evaluation\evaluation.py %arguments%
