import itertools
from typing import List, Callable
from method import Method

class TraceEntry:
    def __init__(self, line: str, findOrInsertMethod: Callable[[int], Method]):
        '''
        Contruct a trace entry from the given trace entry
        '''
        splitLine = line.split()
        if len(splitLine) == 2:
            self.method = findOrInsertMethod(int(splitLine[0]))
            self.isCall = int(splitLine[1]) == 1
        else:
            raise Exception('Could not parse trace entry from line: "' + line + '"')

    def __str__(self) -> str:
        return str(self.method) + ' ' + ('1' if self.isCall else '0')

    def __eq__(self, other) -> bool:
        return self.method is other.method and self.isCall == other.isCall

    def __hash__(self):
        return hash(str(self))

class ObjectTrace:
    def __init__(self, traceEntries: List[TraceEntry]):
        self.traceEntries = traceEntries
        # Fingerprint is in reverse order -- last call in the trace first
        self.head = list(map(lambda entry: entry.method, itertools.takewhile(lambda entry: entry.isCall, traceEntries)))
        self.fingerprint = list(map(lambda entry: entry.method, itertools.takewhile(lambda entry: not entry.isCall, reversed(traceEntries))))
        self.fingerprint.reverse()

    def __str__(self) -> str:
        return '\n'.join(map(str, self.traceEntries))

    def __hash__(self):
        return hash(self.__str__())

    def __eq__(self, other) -> bool:
        return self.traceEntries == other.traceEntries

    def updateMethodStatistics(self):
        '''
        Update call statistics for all methods associated with the trace.
        Store number of appearances for each method in each part of the trace.
        '''

        # Count the total number of times each method in the trace is seen
        # anywhere. Note that we will be modifying the global method
        for entry in self.traceEntries:
            # Only count returns to avoid double counting the number of methods seen
            if entry.isCall:
                entry.method.seenInTorso += 1

        for headMethod in self.head:
            headMethod.seenInHead += 1
            headMethod.seenInTorso -= 1

        # Count the number of methods seen in the fingerprint
        for fingerprintMethod in self.fingerprint:
            fingerprintMethod.seenInFingerprint += 1
            fingerprintMethod.seenInTorso -= 1

        # the initializer is the first method in the trace (assuming first entry is call)
        if self.traceEntries[0].isCall:
            self.traceEntries[0].method.isInitializer = True

        # the finalizer is the last method in the trace (assuming last entry is return)
        if not self.traceEntries[-1].isCall:
            self.traceEntries[-1].method.isFinalizer = True

    def methods(self):
        '''
        Return a list of methods in the trace.
        '''
        return map(lambda entry: entry.method, filter(lambda entry: entry.isCall, self.traceEntries))

    def split(self):
        '''
        Given a set of destructors, return a list of traces created from this
        one. Returns just itself if no splitting is necessary.
        '''
        splitTraces: List[List[TraceEntry]] = []
        currTrace: List[TraceEntry] = []

        entriesIter = iter(self.traceEntries)
        currEntry: TraceEntry = None

        def iterateAndInsert() -> TraceEntry:
            ce = next(entriesIter, None)
            if ce is not None:
                currTrace.append(ce)
            return ce

        currEntry = iterateAndInsert()
        while currEntry is not None:
            # If entry is a finalizer and we are returning from it, potentially split trace
            if currEntry.method.isFinalizer and not currEntry.isCall:
                # If next entry is an initializer, split the trace
                currEntry = iterateAndInsert()
                if currEntry is not None and currEntry.method.isInitializer and currEntry.isCall:
                    splitTraces.append(currTrace[0:-1])
                    currTrace = [currEntry]

                # Otherwise don't split the trace
            currEntry = iterateAndInsert()

        if currTrace != []:
            splitTraces.append(currTrace)

        # Validate the traces generated are valid (all traces must have at least
        # two entries) and none of the entries in any trace should be None
        for trace in splitTraces:
            # assert (len(trace) >= 2), f'len trace = {len(trace)} {trace[0].method} {[[str(t) for t in trace] for trace in splitTraces]}'
            # assert (trace[0].isCall), f'method not call {[[str(t) for t in trace] for trace in splitTraces]}'
            for entry in trace:
                assert(entry is not None)

        return map(ObjectTrace, splitTraces)
