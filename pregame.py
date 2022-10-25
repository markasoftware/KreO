import argparse

parser = argparse.ArgumentParser(description='Perform initial static analysis on a binary')
parser.add_argument('-o', '--output', default='pregame-output', type=argparse.FileType('w', encoding='latin-1'),
                    help='Where to write the output, which must be passed to dynamic analysis')
parser.add_argument('--emulated-cfg', action='store_true',
                    help='If set, use an emulated CFG instead of a "fast" CFG. Usually does not help.')
parser.add_argument('binary', # can't do type=open because angr.project wants a string
                    help='Path to the binary to analyze')

args = parser.parse_args()

print('Loading angr...')
import angr

print('Loading binary...')
# TODO: determine whether we should set the base addr, and if not, how to access it later to correct all the 
project = angr.Project(args.binary, auto_load_libs=False)

if args.emulated_cfg:
    print('Analyzing CFG (Emulated)...')
    cfg = project.analyses.CFGEmulated()
else:
    print('Analyzing CFG (fast)...')
    cfg = project.analyses.CFGFast()

print('Analyzing calling conventions...')
project.analyses.CompleteCallingConventions(cfg=cfg)
print('Narrowing down to method candidates...')
# TODO: The calling convention detector seems to suck. Until it's better, let's consider all the functions
method_candidates = cfg.functions

# for addr in cfg.functions:
#     function = cfg.functions.get_by_addr(addr)
#     print(hex(addr), function.calling_convention, function.prototype)
# A "method candidate" on x86 is any `thiscall` procedure (or fastcall and single-argument) TODO: are there ever cases it thinks a procedure is fastcall with multiple arguments when it's actually thiscall?
# On x64, it's any procedure that takes at least one argument.

# TODO: would like to serialize the CFG so that we don't need to re-read it in postgame, or so that we could quickly run postgame multiple times, but alas there seem to be at least a few bugs rn.

print('Printing output to file...')

args.output.write('[binary]\n')
args.output.write(args.binary + '\n')
args.output.write('[method-candidates]\n')
args.output.write(str(len(method_candidates)))
# adjust all addresses to be relative to base address
args.output.write('\n'.join(map(lambda addr: addr - project.loader.min_addr, method_candidates)) + '\n')
print('DONE successfully!')
