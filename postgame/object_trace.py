import itertools

from typing import List
from method import Method

class TraceEntry:
    def __init__(self, method: Method, is_call: bool):
        self.method = method
        self.is_call = is_call

    def __str__(self) -> str:
        return str(self.method) + (' 1' if self.is_call else '')

    def __eq__(self, other) -> bool:
        return self.method is other.method and self.is_call == other.is_call

class ObjectTrace:
    def __init__(self, trace_entries: List[TraceEntry]):
        self.trace_entries = trace_entries
        # Fingerprint is in reverse order -- last call in the trace first
        self.head = list(map(lambda entry: entry.method, itertools.takewhile(lambda entry: entry.is_call, trace_entries)))
        self.tail = list(map(lambda entry: entry.method, itertools.takewhile(lambda entry: not entry.is_call, reversed(trace_entries))))
        self.tail.reverse()

    def __str__(self) -> str:
        return '\n'.join(map(str, self.trace_entries))

    def __hash__(self):
        return hash(self.__str__())

    def __eq__(self, other) -> bool:
        return self.trace_entries == other.trace_entries

    def updateMethodStatistics(self):
        '''
        Update call statistics for all methods associated with the trace.
        Store number of appearances for each method in each part of the trace.
        '''

        # Count the total number of times each method in the trace is seen
        # anywhere. Note that we will be modifying the global method (each trace
        # may contain references to the same method)
        for entry in self.trace_entries:
            # Only count returns to avoid double counting the number of methods seen
            if entry.is_call:
                if entry.method not in self.head and entry.method not in self.tail:
                    entry.method.seen_in_torso += 1

        for head_method in self.head:
            head_method.seen_in_head += 1

        # Count the number of methods seen in the tail
        for tail_method in self.tail:
            tail_method.seen_in_tail += 1

        # the initializer is the first method in the trace (assuming first entry is call)
        if self.trace_entries[0].is_call:
            self.trace_entries[0].method.is_initializer = True

        # the finalizer is the last method in the trace (assuming last entry is return)
        if not self.trace_entries[-1].is_call:
            self.trace_entries[-1].method.is_finalizer = True

    def methods(self):
        '''
        Return a list of methods in the trace.
        '''
        return map(lambda entry: entry.method, filter(lambda entry: entry.is_call, self.trace_entries))

    def split(self):
        '''
        Given a set of destructors, return a list of traces created from this
        one. Returns just itself if no splitting is necessary.
        '''
        split_traces: List[List[TraceEntry]] = []
        cur_trace: List[TraceEntry] = []

        entries_iter = iter(self.trace_entries)

        def iterateAndInsert() -> TraceEntry:
            ce = next(entries_iter, None)
            if ce is not None:
                cur_trace.append(ce)
            return ce

        cur_entry = iterateAndInsert()
        while cur_entry is not None:
            # If entry is a finalizer and we are returning from it, potentially split trace
            if cur_entry.method.is_finalizer and not cur_entry.is_call:
                # If next entry is an initializer, split the trace
                cur_entry = iterateAndInsert()
                if cur_entry is not None and cur_entry.method.is_initializer and cur_entry.is_call:
                    split_traces.append(cur_trace[:-1])
                    cur_trace = [cur_entry]

                # Otherwise don't split the trace
            cur_entry = iterateAndInsert()

        if len(cur_trace) > 0:
            split_traces.append(cur_trace)

        return map(ObjectTrace, split_traces)
