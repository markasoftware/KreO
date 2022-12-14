import os
import pathlib
from parseconfig import config

scriptpath = pathlib.Path(__file__).parent.absolute()

def boolLowercase(b):
    return 'true' if b else 'false'

executable_path = os.path.join(scriptpath, 'pregame/pregame')
args = [executable_path,
        '--enable-alias-analysis', boolLowercase(config['enableAliasAnalysis']),
        '--enable-calling-convention-analysis', boolLowercase(config['enableCallingConventionAnalysis']),
        '--enable-symbol-procedure-detection', boolLowercase(config['enableSymbolProcedureDetection']),
        '--method-candidates-path', config['methodCandidatesPath'],
        '--static-traces-path', config['staticTracesPath'],
        '--base-offset-path', config['baseOffsetPath']]
if 'debugFunction' in config:
    args += ['--debug-function', str(config['debugFunction'])]

args += ['--', config['binaryPath']]

os.execvp(executable_path, args)
