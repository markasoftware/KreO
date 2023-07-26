from object_trace import TraceEntry, ObjectTrace
from typing import Set, List, Tuple
from method_store import MethodStore

def getBaseOffset(config) -> int:
    return int(next(iter(open(config['baseOffsetPath']))), 16)

def parseInput(config, method_store: MethodStore) -> Tuple[int, Set[ObjectTrace]]:
    traces: Set[ObjectTrace] = set()
    base_offset = getBaseOffset(config)

    # parse blacklisted methods
    blacklisted_methods: Set[int] = set()
    for line in open(config['blacklistedMethodsPath']):
        blacklisted_methods.add(int(line, 16))

    def addIfValid(trace_entries: List[TraceEntry]):
        # Adds the list of trace entries to set of collected traces, assuming
        # the list of entries isn't empty.
        if trace_entries != []:
            traces.add(ObjectTrace(trace_entries))

    cur_trace: List[TraceEntry] = []
    # there can be multiple object trace files...find all of them
    for line in open(config['objectTracesPath']):
        # each line ends with \n, empty line indicates new trace
        if line == '\n':
            addIfValid(cur_trace)
            cur_trace = []
        else:
            split_line = line.split()
            addr = int(split_line[0], 16)
            if addr not in blacklisted_methods:
                method = method_store.findOrInsertMethod(addr)
                is_call = len(split_line) == 2  # the trace entry being a call is identified by a trailing "1" after the address
                cur_trace.append(TraceEntry(method, is_call))
    # finish the last trace
    addIfValid(cur_trace)

    return base_offset, traces
