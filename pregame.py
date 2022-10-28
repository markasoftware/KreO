import sys
import json5

if len(sys.argv) != 2:
    print('Usage: python pregame.py /path/to/config.json')
    exit(1)

config = json5.load(open(sys.argv[1]))
if not 'cfgMode' in config:
    config['cfgMode'] = 'fast'
if not 'methodCandidatesPah' in config:
    config['methodCandidatesPath'] = 'method-candidates'

print('Loading angr...')
import angr

print('Loading binary...')
# TODO: determine whether we should set the base addr, and if not, how to access it later to correct all the 
project = angr.Project(config['binaryPath'], auto_load_libs=False)

if config['cfgMode'] == 'emulated':
    print('Analyzing CFG (Emulated)...')
    cfg = project.analyses.CFGEmulated()
elif config['cfgMode'] == 'fast':
    print('Analyzing CFG (fast)...')
    cfg = project.analyses.CFGFast()
else:
    print('Warning: unknown CFG mode! Use emulated or fast.')
    exit(1)

print('Analyzing calling conventions...')
project.analyses.CompleteCallingConventions(cfg=cfg)
print('Narrowing down to method candidates...')
# TODO: The calling convention detector seems to suck. Until it's better, let's consider all the functions
# TODO: function detector seems to suck too! Maybe this whole project sucks!
method_candidates = cfg.functions

# for addr in cfg.functions:
#     function = cfg.functions.get_by_addr(addr)
#     print(hex(addr), function.calling_convention, function.prototype)
# A "method candidate" on x86 is any `thiscall` procedure (or fastcall and single-argument) TODO: are there ever cases it thinks a procedure is fastcall with multiple arguments when it's actually thiscall?
# On x64, it's any procedure that takes at least one argument.

# TODO: would like to serialize the CFG so that we don't need to re-read it in postgame, or so that we could quickly run postgame multiple times, but alas there seem to be at least a few bugs rn.

print('Printing output to file...')

# adjust all addresses to be relative to base address
open(config['methodCandidatesPath'], 'w').write('\n'.join(map(lambda addr: str(addr - project.loader.min_addr), method_candidates)) + '\n')
print('DONE successfully!')
