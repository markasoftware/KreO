import sys
import os
import json5

if len(sys.argv) != 2:
    print('Usage: python game.py /path/to/config.json')
    exit(1)

config = json5.load(open(sys.argv[1]))
if not 'PIN_ROOT' in os.environ:
    print('PIN_ROOT environment variable must be set to where you downloaded the pin kit.')
    exit(1)

# TODO: make it better at figuring out where the pintool is cross-platform!
pin_executable_path = os.environ['PIN_ROOT'] + '/pin'
os.execvp(pin_executable_path, [pin_executable_path, '-t', sys.argv[0] + '/pintool/obj-intel64/Game.so', '--', config['binaryPath']])
