'''
Standalone utility for checking the validity of an object-trace

An object trace is valid if:
- each return has an associated call
'''

import sys
import subprocess
import pathlib
import os
from typing import List, Set

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..'))
sys.path.append(os.path.join(fpath, '..', '..'))

from method_store import MethodStore
from object_trace import ObjectTrace, TraceEntry
from parseconfig import parseconfig_argparse

config = parseconfig_argparse()

def loadTraces():
    traces: Set[ObjectTrace] = set()
    methodStore = MethodStore()

    curTrace: List[TraceEntry] = []
    # there can be multiple object trace files...find all of them

    for line in open(config['objectTracesPath']):
        # each line ends with \n, empty line indicates new trace
        if len(line) == 1:
            if curTrace:
                traces.add(ObjectTrace(curTrace))
                curTrace = []
        else:
            curTrace.append(TraceEntry(line, methodStore.findOrInsertMethod, 0))
    # finish the last trace
    if curTrace:
        traces.add(ObjectTrace(curTrace))

    for line in open(config['objectTracesPath'] + '-name-map'):
        splitlines = line.split()
        try:
            p1 = subprocess.Popen(['demangle', '--noerror', '-n', splitlines[1]], stdout=subprocess.PIPE)
            demangled_name = str(p1.stdout.read())
        except FileNotFoundError as _:
            demangled_name = splitlines[1]
        methodStore.insertMethodName(int(splitlines[0]), demangled_name)

    return traces, methodStore

def main():
    print('loading traces')
    traces, _ = loadTraces()
    print('traces loaded, validating')

    for trace in traces:
        entryStack: List[TraceEntry] = []
        for entry in trace.traceEntries:
            if entry.isCall:
                entryStack.append(entry)
            else:
                assert len(entryStack) != 0, f'object-trace invalid, call/returns don\'t match {trace}'
                assert entryStack[-1].method == entry.method, f'object-trace invalid, call/returns don\'t match {trace}'
                entryStack.pop()
    
        assert len(entryStack) == 0, f'object-trace invalid, call/returns don\'t match {trace}'

if __name__ == '__main__':
    main()
