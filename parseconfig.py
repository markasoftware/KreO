import sys
import json5
import pathlib
from os.path import join

if len(sys.argv) != 2:
    print('Usage: python ' + sys.argv[0] + ' /path/to/config.json')
    exit(1)


scriptpath = pathlib.Path(__file__).parent.absolute()

config = json5.load(open(sys.argv[1]))
if not 'cfgMode' in config:
    config['cfgMode'] = 'fast'
if not 'methodCandidatesPath' in config:
    config['methodCandidatesPath'] = join(scriptpath, 'out/method-candidates')
if not 'gtMethodsPath' in config:
    config['gtMethodsPath'] = join(scriptpath, 'out/gt-methods')
if not 'gtMethodsInstrumentedPath' in config:
    config['gtMethodsInstrumentedPath'] = join(scriptpath, 'out/gt-methods-instrumented')
if not 'objectTracesPath' in config:
    config['objectTracesPath'] = join(scriptpath, 'out/object-traces')
if not 'resultsPath' in config:
    config['resultsPath'] = join(scriptpath, 'out/results.json')
if not 'resultsIndent' in config:
    config['resultsIndent'] = 4
