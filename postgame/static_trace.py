from typing import List, Callable
from method import Method

StaticTraceEntry = int
# class StaticTraceEntry:
#     def __init__(self, line: str, baseAddr: int):
#         splitLine = line.split()
#         self.addr = int(splitLine[0])
#         # Name is in there mainly for debugging looking at the trace manually -- don't really need it.

#     def __str__(self) -> str:
#         return str(self.method)

#     def __eq__(self, other) -> bool:
#         return self.method is other.method

#     def __hash__(self):
#         return hash(self.__str__())

class StaticTrace:
    def __init__(self, traceEntries: List[StaticTraceEntry]):
        self.entries = traceEntries

    def __str__(self):
        return '\n'.join(map(str, self.entries))

    def __hash__(self):
        return hash(self.__str__())

    def __eq__(self, other):
        return self.entries == other.entries

    def split(self):
        '''
        Returns a list of updated traces, by splitting after any destructors. Uses method.isFinalizer to determine destructors, so ensure that statistics are up-to-date.
        '''
        result: List[List[TraceEntry]] = [StaticTrace([])] # start with single empty trace

        for entry in self.entries:
            splitTraces[-1].entries.append(entry)
            if entry.method.isFinalizer:
                result.append(StaticTrace([]))

        # If last entry is a finalizer, may end up with an extra empty trace at the end.
        if len(result[-1].entries) == 0:
            pop(result)

        return result
