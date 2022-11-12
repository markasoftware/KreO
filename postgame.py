import json
import itertools
from parseconfig import config

procedures = dict() # map from address to procedure

class Procedure:
    def __init__(self, address, name='N/A'):
        self.address = address
        self.name = name
        # How many times the procedure has been seen in different parts of a trace:
        self.seenTail = 0
        self.seenTorso = 0

    def isDestructor(self):
        return self.seenTail > 0 and self.seenTorso <= self.seenTail//4 # TODO: more thought into this, other heuristics?

def findProcedure(address, name='N/A'):
    if address not in procedures:
        procedures[address] = Procedure(address, name)
    return procedures[address]

class TraceEntry:
    def __init__(self, line):
        splitLine = line.split()
        if len(splitLine) == 2:
            self.procedure = findProcedure(int(splitLine[0]))
            self.isCall = int(splitLine[1]) == 1
        elif len(splitLine) == 3:
            self.procedure = findProcedure(int(splitLine[1]), splitLine[0])
            self.isCall = int(splitLine[2]) == 1
        else:
            raise Exception('Could not parse trace entry from line: "' + line + '"')

    def __str__(self):
        procStr = ('' if self.procedure.name == 'N/A' else (self.procedure.name + ' ')) + str(self.procedure.address)
        return procStr + ' ' + ('1' if self.isCall else '0') + '\n'

    def __eq__(self, other):
        return self.procedure is other.procedure and self.isCall == other.isCall

class Trace:
    def __init__(self, traceEntries):
        self.traceEntries = traceEntries
        # Tail is in reverse order -- last call in the trace first
        self.tail = list(map(lambda entry: entry.procedure, itertools.takewhile(lambda entry: not entry.isCall, reversed(traceEntries))))
        self.numUpperBodyEntries = len(traceEntries) - len(self.tail)

    def __str__(self):
        return ''.join(map(lambda te: te.__str__(), self.traceEntries))

    def __hash__(self):
        return hash(self.__str__())

    # Return a generator for entries preceding the tail
    def torso(self):
        return itertools.islice(self.traceEntries, self.numUpperBodyEntries)

    # update all procedures involved with this trace, to store number of appearances in each part of the trace.
    def updateProcedureStatistics(self):
        for torsoEntry in self.torso():
            if not torsoEntry.isCall: # avoid double-counting, and avoid counting the calls corresponding to the returns in the tail
                torsoEntry.procedure.seenTorso += 1
        for tailProcedure in self.tail:
            tailProcedure.seenTail += 1

    # Given a set of destructors, return a list of traces created from this one. Returns just itself if no splitting is necessary.
    def split(self, destructors):
        # basically a state machine as we iterate through the entries. When we find a destructor, switch to a state where we're essentially trying to end the sub-trace as soon as we can.
        result = []
        curResultTrace = []
        isTerminatingTrace = False
        for entry in self.traceEntries:
            if isTerminatingTrace:
                if entry.isCall: # the tail is over, rejoice or something
                    result.append(curResultTrace)
                    curResultTrace = [entry]
                    isTerminatingTrace = False
                else:
                    curResultTrace.append(entry)
            else: # not terminating the trace
                curResultTrace.append(entry)
                if entry.procedure in destructors:
                    isTerminatingTrace = True
        # end of loop
        if curResultTrace:
            result.append(curResultTrace)
        return map(Trace, result)

def printTraces(traces):
    for trace in traces:
        print(trace)

# Step 1: Read traces from disk

def parseTraces():
    curTrace = []
    traces = []
    for line in open(config['objectTracesPath']):
        # each line ends with \n, empty line indicates new trace
        if len(line) == 1:
            if curTrace:
                traces.append(Trace(curTrace))
                curTrace = []
        else:
            curTrace.append(TraceEntry(line))
    # finish the last trace
    if curTrace:
        traces.append(Trace(curTrace))
    return traces
traces = parseTraces()

# Remove duplicate traces. TODO: check that the hash function that set() uses is appropriate.
def removeDuplicateTraces():
    global traces
    result = []
    tracesSet = set()
    for trace in traces:
        if not trace in tracesSet:
            tracesSet.add(trace)
            result.append(trace)
    traces = result
removeDuplicateTraces()

# Step 2: Record how many times each procedure was seen in each part of the trace.

for trace in traces:
    trace.updateProcedureStatistics()

# Step 3: Decide what's a destructor
destructors = set(filter(lambda proc: proc.isDestructor(), procedures.values()))
print('fini:')
print(procedures[4528].seenTail)
print(procedures[4528].seenTorso)
print(procedures[4528].isDestructor())

# Step 4: Split traces based on destructors
splitTraces = []
for trace in traces:
    splitTraces += trace.split(destructors)

print('Split traces:')
printTraces(splitTraces)

# TODO: Possible improvement: Perform the last steps iteratively. I.e., if splitting reveals a new destructor, then we may want to re-split. However, splitting seems like it shouldn't be happening too often, let alone re-splitting.

# TODO: possible improvement: Instead of just looking for the true tail of returns, maybe if we identify a suffix common to many object traces, we can infer that a destructor is calling another method and account for it?

# Step 5: Look at tails to determine hierarchy
# Ideally, everything would form a nice trie. But what if it doesn't, eg in the case when we're just tracing weird sequence of functions that operate on an integer pointer?

# Step 6: Associate methods from torsos with classes
# Step 7: Static dataflow analysis
