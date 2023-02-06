import sys
import json5
import pathlib
from os.path import join
import os.path

if len(sys.argv) != 2:
    print('Usage: python ' + sys.argv[0] + ' /path/to/config.json')
    exit(1)


scriptpath = pathlib.Path(__file__).parent.absolute()

config = json5.load(open(sys.argv[1]))
if not 'cfgMode' in config:
    config['cfgMode'] = 'fast'
if not 'enableAliasAnalysis' in config:
    config['enableAliasAnalysis'] = True
if not 'enableCallingConventionAnalysis' in config:
    config['enableCallingConventionAnalysis'] = True
if not 'enableSymbolProcedureDetection' in config:
    config['enableSymbolProcedureDetection'] = False
if not 'resultsIndent' in config:
    config['resultsIndent'] = 4

if not 'baseDirectory' in config:
    config['baseDirectory'] = 'out'
baseDirectory = config['baseDirectory']
# ensure it exists
if not os.path.exists(baseDirectory):
    os.mkdir(baseDirectory)

config['methodCandidatesPath'] = join(scriptpath, baseDirectory, 'method-candidates')
config['blacklistedMethodsPath'] = join(scriptpath, baseDirectory, 'blacklisted-methods')
config['gtMethodsPath'] = join(scriptpath, baseDirectory, 'gt-methods')
config['gtMethodsInstrumentedPath'] = join(scriptpath, baseDirectory, 'gt-methods-instrumented')
config['baseOffsetPath'] = join(scriptpath, baseDirectory, 'base-address')
config['staticTracesPath'] = join(scriptpath, baseDirectory, 'static-traces')
config['objectTracesPath'] = join(scriptpath, baseDirectory, 'object-traces')
config['resultsPath'] = join(scriptpath, baseDirectory, 'results.json')
