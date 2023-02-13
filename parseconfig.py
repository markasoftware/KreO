# NOTE: all paths specified in the config are relative
# to the base directory (specified by baseDirectory)
# except for baseDirectory, which is relative to the json file

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
if not 'pdbFile' in config:
    config['pdbFile'] = 'project.pdb'
if not 'resultsPath' in config:
    config['resultsPath'] = 'results.txt'
if not 'resultsInstrumentedPath' in config:
    config['resultsInstrumentedPath'] = 'results-instrumented.txt'
if not 'baseDirectory' in config:
    config['baseDirectory'] = 'out'

config_path = pathlib.Path(config_fname).parent.absolute()

# Set baseDirectory relative to config_path
config['baseDirectory'] = join(config_path, config['baseDirectory'])

# ensure it exists
if not os.path.exists(config['baseDirectory']):
    os.mkdir(config['baseDirectory'])

def PathRelBase(path):
    return join(config['baseDirectory'], path)

config['methodCandidatesPath'] = PathRelBase('method-candidates')
config['blacklistedMethodsPath'] = PathRelBase('blacklisted-methods')
config['gtMethodsPath'] = PathRelBase('gt-methods')
config['gtMethodsInstrumentedPath'] = PathRelBase('gt-methods-instrumented')
config['baseOffsetPath'] = PathRelBase('base-address')
config['staticTracesPath'] = PathRelBase('static-traces')
config['objectTracesPath'] = PathRelBase('object-traces')
config['resultsJson'] = PathRelBase('results.json')
config['gtResultsJson'] = PathRelBase('gt-results.json')
config['pdbFile'] = PathRelBase(config['pdbFile'])
config['dumpFile'] = PathRelBase('project.dump')
config['binaryPath'] = PathRelBase(config['binaryPath'])
config['resultsPath'] = PathRelBase(config['resultsPath'])
config['resultsInstrumentedPath'] = PathRelBase(config['resultsInstrumentedPath'])
