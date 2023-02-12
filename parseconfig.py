import sys
import json5
import pathlib
from os.path import join
import os.path

if len(sys.argv) != 2:
    print('Usage: python ' + sys.argv[0] + ' /path/to/config.json')
    exit(1)

config_fname = sys.argv[1]
config = json5.load(open(config_fname))
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

config_path = pathlib.Path(config_fname).parent.absolute()

config['methodCandidatesPath'] = join(config_path, baseDirectory, 'method-candidates')
config['blacklistedMethodsPath'] = join(config_path, baseDirectory, 'blacklisted-methods')
config['gtMethodsPath'] = join(config_path, baseDirectory, 'gt-methods')
config['gtMethodsInstrumentedPath'] = join(config_path, baseDirectory, 'gt-methods-instrumented')
config['baseOffsetPath'] = join(config_path, baseDirectory, 'base-address')
config['staticTracesPath'] = join(config_path, baseDirectory, 'static-traces')
config['objectTracesPath'] = join(config_path, baseDirectory, 'object-traces')
config['resultsPath'] = join(config_path, baseDirectory, 'results.json')
