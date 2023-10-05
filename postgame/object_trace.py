from dataclasses import dataclass
from functools import cache

from typing_extensions import Self

from postgame.method import Method


@dataclass
class TraceEntry:
    method: Method
    is_call: bool

    def __str__(self) -> str:
        return str(self.method) + (" 1" if self.is_call else "")

    def __eq__(self, other: Self) -> bool:
        return self.method is other.method and self.is_call == other.is_call


class ObjectTrace:
    def __init__(self, trace_entries: list[TraceEntry]):
        self.trace_entries = trace_entries

        i = 0
        for i in range(len(trace_entries)):
            if not trace_entries[i].is_call:
                break

        self.head = self.trace_entries[: i + 1]

        i = 0
        for i in range(len(trace_entries)):
            if trace_entries[len(trace_entries) - i - 1].is_call:
                break

        self.tail = list(reversed(self.trace_entries[: len(trace_entries) - i - 1]))

    def analyze_head_tail(self) -> list[Method] | None:
        """
        Returns:
            None if the object trace is invalid.
        """
        if len(self.head) + len(self.tail) > len(self.trace_entries):
            # Head and tail overlap.
            return None

        if len(self.head) < len(self.tail):
            # Likely extra methods in the tail
            self.tail = self.tail[: len(self.head)]
        elif len(self.head) > len(self.tail):
            # Likely extra methods in the head
            self.head = self.head[: len(self.tail)]

        # The same number of methods in the head and tail
        for te in self.head:
            te.method.is_constructor = True
        for te in self.tail:
            te.method.is_destructor = True

    def update_method_statistics(self):
        """
        Update call statistics for all methods associated with the trace.
        Store number of appearances for each method in each part of the trace.
        """

        # Count the total number of times each method in the trace is seen
        # anywhere. Note that we will be modifying the global method (each trace
        # may contain references to the same method)
        # for entry in self.trace_entries:
        #     # Only count returns to avoid double counting the number of methods seen
        #     if entry.is_call:
        #         if entry.method not in self.head and entry.method not in self.tail:
        #             entry.method.seen_in_torso += 1

        # for head_method in self.head:
        #     head_method.seen_in_head += 1

        # # Count the number of methods seen in the tail
        # for tail_method in self.tail:
        #     tail_method.seen_in_tail += 1

        if len(self.trace_entries) > 0:
            if not self.trace_entries[0].is_call:
                raise RuntimeError("first entry in object trace not a call")
            self.trace_entries[0].method.is_initializer = True

            if self.trace_entries[-1].is_call:
                raise RuntimeError("last entry in object trace not a return")
            self.trace_entries[-1].method.is_finalizer = True

    def methods(self):
        """
        Return a list of methods in the trace.
        """
        return map(
            lambda entry: entry.method,
            filter(lambda entry: entry.is_call, self.trace_entries),
        )

    def split(self):
        """
        Given a set of destructors, return a list of traces created from this
        one. Returns just itself if no splitting is necessary.
        """
        split_traces: list[list[TraceEntry]] = []
        cur_trace: list[TraceEntry] = []

        entries_iter = iter(self.trace_entries)

        def iterateAndInsert() -> TraceEntry | None:
            ce = next(entries_iter, None)
            if ce is not None:
                cur_trace.append(ce)
            return ce

        cur_entry = iterateAndInsert()
        while cur_entry is not None:
            # If entry is a finalizer and we are returning from it, potentially split
            # trace
            if cur_entry.method.is_finalizer and not cur_entry.is_call:
                # If next entry is an initializer, split the trace
                cur_entry = iterateAndInsert()
                if (
                    cur_entry is not None
                    and cur_entry.method.is_initializer
                    and cur_entry.is_call
                ):
                    split_traces.append(cur_trace[:-1])
                    cur_trace = [cur_entry]

                # Otherwise don't split the trace
            cur_entry = iterateAndInsert()

        if len(cur_trace) > 0:
            split_traces.append(cur_trace)

        return map(ObjectTrace, split_traces)

    def __str__(self) -> str:
        return "\n".join(map(str, self.trace_entries))

    def __hash__(self):
        return hash(self.__str__())

    def __eq__(self, other: Self) -> bool:
        return self.trace_entries == other.trace_entries
