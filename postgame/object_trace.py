import logging
from dataclasses import dataclass
from enum import StrEnum, auto
from typing import Iterable

from typing_extensions import Self

from postgame.method import Method

LOGGER = logging.getLogger(__name__)


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

    def __hash__(self) -> int:
        # we only care about the method's address and is_call
        return self.method.address | (int(self.is_call) << 31)


class ObjectTrace:
    """
    Class that contains an object trace. An object trace is a list of trace
    entries.
    """

    def __init__(self, trace_entries: list[TraceEntry]):
        self.__trace_entries = trace_entries

        self.__head_calls = 0
        self.__tail_returns = 0

        self.__head_length = 0
        self.__tail_length = 0

        self.__hash_value: int | None = None

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

        for te in trace_entries:
            if te.is_call:
                te_stack += 1
            else:
                te_stack -= 1
        if te_stack != 0:
            msg = f"Failed to parse object trace, calls and returns don't match: {self}"
            LOGGER.error(msg)

    def get_trace_entries(self):
        return self.__trace_entries

    def head_calls(self):
        return self.__trace_entries[: self.__head_calls]

    def tail_returns(self):
        return list(
            reversed(
                self.__trace_entries[len(self.__trace_entries) - self.__tail_returns :],
            )
        )

    def head(self):
        return self.__trace_entries[: self.__head_length]

    def tail(self):
        return self.__trace_entries[-self.__tail_length :]

    def identify_initializer_finalizer(self):
        """
        Identify methods that are initializers and finalizers. The method associated
        with the first trace entry is an initializer and the method associated with the
        last trace entry is a finalizer.
        """
        if self.__trace_entries != []:
            self.__trace_entries[0].method.is_initializer = True
            self.__trace_entries[-1].method.is_finalizer = True

    def update_head_tail(self):
        """
        Remove trace entries from the head and tail based on the following heuristics:

        - If the head and tail overlap, remove all trace entries from the head and tail.
        - If there are more trace entries in either the head or tail, remove trace
          entries from either the head or tail (whichever is larger).

        This is not something that the original Lego paper does, and should not be
        called when running Lego.
        """
        if self.__tail_length + self.__head_length > len(self.__trace_entries):
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
            self.__trace_entries[i].method.seen_in_head += 1

        for i in range(self.__tail_returns):
            self.__trace_entries[
                len(self.__trace_entries) - i - 1
            ].method.seen_in_tail += 1

        for te in self.__trace_entries:
            if te.is_call:
                te.method.seen_count += 1

    def methods(self) -> set[Method]:
        """
        Return a set of methods in the trace.
        """
        return set(
            map(
                lambda entry: entry.method,
                filter(lambda entry: entry.is_call, self.__trace_entries),
            )
        )

    def split(self) -> None | Iterable[Self]:
        """
        Given a set of destructors, return a list of traces created from this
        one. Returns just itself if no splitting is necessary.
        """
        split_traces: list[list[TraceEntry]] = []

        trace_start_idx = 0

        for i in range(len(self.__trace_entries)):
            cur_entry = self.__trace_entries[i]
            if (
                cur_entry.method.is_finalizer
                and not cur_entry.is_call
                and i + 1 < len(self.__trace_entries)
                and self.__trace_entries[i + 1].method.is_initializer
                and self.__trace_entries[i + 1].is_call
            ):
                split_traces.append(self.__trace_entries[trace_start_idx : i + 1])
                trace_start_idx = i + 1

        if split_traces == []:
            return None

        split_traces.append(self.__trace_entries[trace_start_idx:])

        return map(ObjectTrace, split_traces)

    def __str__(self) -> str:
        return "\n".join(map(str, self.__trace_entries))

    def __hash__(self):
        # when hashing we only care about self.trace_entries
        if not self.__hash_value:
            self.__hash_value = hash(tuple([hash(x) for x in self.__trace_entries]))
        return self.__hash_value

    def __eq__(self, other: Self) -> bool:
        return self.__trace_entries == other.__trace_entries
