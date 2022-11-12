import sys
import json5

if len(sys.argv) != 2:
    print('Usage: python ' + sys.argv[0] + ' /path/to/config.json')
    exit(1)

config = json5.load(open(sys.argv[1]))
if not 'cfgMode' in config:
    config['cfgMode'] = 'fast'
if not 'methodCandidatesPath' in config:
    config['methodCandidatesPath'] = 'out/method-candidates'
if not 'objectTracesPath' in config:
    config['objectTracesPath'] = 'out/object-traces'
