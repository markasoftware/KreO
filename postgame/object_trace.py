from dataclasses import dataclass
from enum import StrEnum, auto

from typing_extensions import Self

from postgame.method import Method


class TEType(StrEnum):
    HEAD = auto()
    TAIL = auto()
    BODY = auto()


@dataclass
class TraceEntry:
    """Data class for trace entry. A method reference and whether or not that method is
    being called."""

    method: Method
    is_call: bool

    def __str__(self) -> str:
        return str(self.method) + (" 1" if self.is_call else "")

    def __eq__(self, other: Self) -> bool:
        return self.method is other.method and self.is_call == other.is_call


class ObjectTrace:
    """
    Class that contains an object trace. An object trace is a list of trace
    entries.
    """

    def __init__(self, trace_entries: list[TraceEntry]):
        self.trace_entries = trace_entries

        self.__head_calls = 0
        self.__tail_returns = 0

        self.__head_length = 0
        self.__tail_length = 0

        i = 0
        te_stack = 0
        counting_head_calls = True
        for i in range(len(trace_entries)):
            if trace_entries[i].is_call:
                if counting_head_calls:
                    self.__head_calls += 1
                te_stack += 1
            else:
                te_stack -= 1
                counting_head_calls = False

            self.__head_length += 1

            if te_stack <= 0:
                break

        i = 0
        te_stack = 0
        counting_tail_returns = True
        for i in range(len(trace_entries)):
            if trace_entries[len(trace_entries) - i - 1].is_call:
                te_stack -= 1
                counting_tail_returns = False
            else:
                if counting_tail_returns:
                    self.__tail_returns += 1
                te_stack += 1

            self.__tail_length += 1

            if te_stack <= 0:
                break

    def head_calls(self):
        return self.trace_entries[: self.__head_calls]

    def tail_returns(self):
        return list(
            reversed(
                self.trace_entries[len(self.trace_entries) - self.__tail_returns :],
            )
        )

    def head(self):
        return self.trace_entries[: self.__head_length]

    def tail(self):
        return self.trace_entries[-self.__tail_length :]

    def identify_initializer_finalizer(self):
        """
        Identify methods that are initializers and finalizers. The method associated
        with the first trace entry is an initializer and the method associated with the
        last trace entry is a finalizer.
        """
        if self.trace_entries != []:
            self.trace_entries[0].method.is_initializer = True
            self.trace_entries[-1].method.is_finalizer = True

    def update_head_tail(self):
        """
        Remove trace entries from the head and tail based on the following heuristics:

        - If the head and tail overlap, remove all trace entries from the head and tail.
        - If there are more trace entries in either the head or tail, remove trace
          entries from either the head or tail (whichever is larger).

        This is not something that the original Lego paper does, and should not be
        called when running Lego.
        """
        if self.__tail_length + self.__head_length > len(self.trace_entries):
            # Head and tail overlap.
            self.__head_calls = 0
            self.__tail_returns = 0
        elif self.__head_calls < self.__tail_returns:
            # Likely extra methods in the tail
            self.__tail_returns = self.__head_calls
        elif self.__head_calls > self.__tail_returns:
            # Likely extra methods in the head
            self.__head_calls = self.__tail_returns

    def update_method_statistics(self):
        for i in range(self.__head_calls):
            self.trace_entries[i].method.seen_in_head += 1

        for i in range(self.__tail_returns):
            self.trace_entries[len(self.trace_entries) - i - 1].method.seen_in_tail += 1

        for te in self.trace_entries:
            if te.is_call:
                te.method.seen_count += 1

    def methods(self) -> set[Method]:
        """
        Return a set of methods in the trace.
        """
        return set(
            map(
                lambda entry: entry.method,
                filter(lambda entry: entry.is_call, self.trace_entries),
            )
        )

    def split(self):
        """
        Given a set of destructors, return a list of traces created from this
        one. Returns just itself if no splitting is necessary.
        """
        split_traces: list[list[TraceEntry]] = []
        cur_trace: list[TraceEntry] = []

        entries_iter = iter(self.trace_entries)

        def iterate_and_insert() -> TraceEntry | None:
            ce = next(entries_iter, None)
            if ce is not None:
                cur_trace.append(ce)
            return ce

        cur_entry = iterate_and_insert()
        while cur_entry is not None:
            # If entry is a finalizer and we are returning from it, potentially split
            # trace
            if cur_entry.method.is_finalizer and not cur_entry.is_call:
                # If next entry is an initializer, split the trace
                cur_entry = iterate_and_insert()
                if (
                    cur_entry is not None
                    and cur_entry.method.is_initializer
                    and cur_entry.is_call
                ):
                    split_traces.append(cur_trace[:-1])
                    cur_trace = [cur_entry]

                # Otherwise don't split the trace
            cur_entry = iterate_and_insert()

        if len(cur_trace) > 0:
            split_traces.append(cur_trace)

        return map(ObjectTrace, split_traces)

    def __str__(self) -> str:
        return "\n".join(map(str, self.trace_entries))

    def __hash__(self):
        return hash(self.__str__())

    def __eq__(self, other: Self) -> bool:
        return self.trace_entries == other.trace_entries
