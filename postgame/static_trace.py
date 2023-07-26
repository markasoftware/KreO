from typing import List, Callable
from method import Method

class StaticTraceEntry:
    def __init__(self, line: str, findOrInsertMethod: Callable[[int, bool], Method]):
        assert line[0] != '#', 'Tried to construct StaticTraceEntry from a comment.'
        splitLine = line.split()
        self.method = findOrInsertMethod(int(splitLine[0], 16), False)
        # Name is in there mainly for debugging looking at the trace manually -- don't really need it.

    def __str__(self) -> str:
        return str(self.method)

    def __eq__(self, other) -> bool:
        return self.method is other.method

    def __hash__(self):
        return hash(str(self))

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
        Returns a list of updated traces, by splitting after any destructors.
        Uses method.is_finalizer to determine destructors, so ensure that
        method statistics are up-to-date before splitting.
        '''
        result: List[StaticTrace] = [StaticTrace([])]  # start with single empty trace

        for entry in self.entries:
            # add entry to end of result
            result[-1].entries.append(entry)
            if entry.method.is_finalizer:
                # split the trace if the current method is a finalizer so additional methods
                # are placed in a new static trace.
                result.append(StaticTrace([]))

        # If last entry is a finalizer, may end up with an extra empty trace at the end.
        if len(result[-1].entries) == 0:
            result.pop()

        return result
