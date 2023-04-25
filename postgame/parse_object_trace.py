from object_trace import TraceEntry, ObjectTrace
from typing import Set, List, Tuple

def getBaseOffset(config) -> int:
    return int(next(iter(open(config['baseOffsetPath']))))

def parseInput(config, methodStore) -> Tuple[int, Set[ObjectTrace]]:
    traces: Set[ObjectTrace] = set()
    base_offset = getBaseOffset(config)

    blacklisted_methods: Set[int] = set()
    for line in open(config['blacklistedMethodsPath']):
        blacklisted_methods.add(int(line))

    def addIfValid(trace: List[TraceEntry]):
        if trace != []:
            traces.add(ObjectTrace(trace))

    cur_trace: List[TraceEntry] = []
    # there can be multiple object trace files...find all of them
    for line in open(config['objectTracesPath']):
        # each line ends with \n, empty line indicates new trace
        if line == '\n':
            addIfValid(cur_trace)
            cur_trace = []
        else:
            addr = int(line.split()[1])
            if addr not in blacklisted_methods:
                cur_trace.append(TraceEntry(line, methodStore.findOrInsertMethod))
    # finish the last trace
    addIfValid(cur_trace)

    return base_offset, traces
