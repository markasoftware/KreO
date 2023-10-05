import sys
from pathlib import Path
from typing import List, Set, Tuple

from postgame.method_store import MethodStore
from postgame.object_trace import ObjectTrace, TraceEntry

SCRIPT_PATH = Path(__file__).parent.absolute()

sys.path.append(str(SCRIPT_PATH / ".."))

from parseconfig import Config  # noqa: E402


def get_base_offset(config: Config) -> int:
    return int(next(iter(config.base_offset_path.open())), 16)


def parse_input(
    config: Config,
    method_store: MethodStore,
) -> Tuple[int, Set[ObjectTrace]]:
    traces: Set[ObjectTrace] = set()
    base_offset = get_base_offset(config)

    # parse blacklisted methods
    blacklisted_methods: Set[int] = set()
    for line in config.blacklisted_methods_path.open():
        blacklisted_methods.add(int(line, 16))

    def add_if_valid(trace_entries: List[TraceEntry]):
        # Adds the list of trace entries to set of collected traces, assuming
        # the list of entries isn't empty.
        if trace_entries != []:
            ot_stack_len = 0
            for trace in trace_entries:
                if trace.is_call:
                    ot_stack_len += 1
                else:
                    ot_stack_len -= 1

                if ot_stack_len <= 0:
                    break

            if ot_stack_len == 0:
                traces.add(ObjectTrace(trace_entries))

    cur_trace: List[TraceEntry] = []
    # there can be multiple object trace files...find all of them
    for line in config.object_traces_path.open():
        # each line ends with \n, empty line indicates new trace
        if line == "\n":
            add_if_valid(cur_trace)
            cur_trace = []
        else:
            split_line = line.split()
            addr = int(split_line[0], 16)
            if addr not in blacklisted_methods:
                method = method_store.find_or_insert_method(addr)
                # the trace entry being a call is identified by a trailing "1" after the address
                is_call = len(split_line) == 2
                cur_trace.append(TraceEntry(method, is_call))

    # finish the last trace
    add_if_valid(cur_trace)

    return base_offset, traces
