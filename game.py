import sys
import os
from parseconfig import config

if 'PIN_ROOT' in config:
   pin_root = config['PIN_ROOT']
elif 'PIN_ROOT' in os.environ:
    pin_root = os.environ['PIN_ROOT']
else:
    raise Exception('PIN_ROOT environment variable or config option must be set to where you downloaded the pin kit.')

# TODO: make it better at figuring out where the pintool is cross-platform!
pin_executable_path = os.environ['PIN_ROOT'] + '/pin'

pintool_shared_object = os.path.dirname(os.path.realpath(__file__)) + '/pintool'
if config['isa'] == 'x86':
    pintool_shared_object += '/obj-ia32'
elif config['isa'] == 'x86-64':
    pintool_shared_object += '/obj-intel64'
else:
    raise Exception('Unsupported ISA: "' + config['isa'] + '". Please use x86 or x86-64.')
if sys.platform == "linux" or sys.platform == "linux2" or sys.platform == 'darwin':
    pintool_shared_object += '/Game.so'
elif sys.platform == 'win32':
    pintool_shared_object += '/Game.dll'
else:
    raise Exception('Unsupported operating system: "' + sys.platform + '".')

os.execvp(pin_executable_path, [pin_executable_path, '-t', pintool_shared_object, '--', config['binaryPath']])
