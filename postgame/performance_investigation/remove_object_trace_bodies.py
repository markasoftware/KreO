import sys
from pathlib import Path

SCRIPT_PATH = Path(__file__).parent


sys.path.append(str(SCRIPT_PATH / ".." / ".."))
sys.path.append(str(SCRIPT_PATH / ".."))

from parseconfig import Config  # noqa: E402
from postgame.method_store import MethodStore  # noqa: E402
from postgame.object_trace import ObjectTrace, TraceEntry  # noqa: E402
from postgame.parse_object_trace import parse_input  # noqa: E402


def remove_object_trace_bodies(config: Config):
    method_store = MethodStore()
    _, object_traces = parse_input(config, method_store)

    bodies_removed = config.object_traces_path.with_name(
        config.object_traces_path.name + "-no-bodies"
    )

    with bodies_removed.open("w") as out_file:
        for ot in object_traces:
            trace_entries_head: list[TraceEntry] = []
            ot_stack_len = 0
            head_len = 0
            for entry in ot.trace_entries:
                head_len += 1

                if entry.is_call:
                    ot_stack_len += 1
                else:
                    ot_stack_len -= 1

                trace_entries_head.append(entry)

                if ot_stack_len == 0:
                    break

            assert ot_stack_len == 0

            trace_entries_without_head = ot.trace_entries[head_len:]

            trace_entries_tail: list[TraceEntry] = []
            for entry in reversed(trace_entries_without_head):
                if not entry.is_call:
                    ot_stack_len += 1
                else:
                    ot_stack_len -= 1

                trace_entries_tail.append(entry)

                if ot_stack_len == 0:
                    break

            new_trace_entries = trace_entries_head + list(reversed(trace_entries_tail))

            assert ot_stack_len == 0

            ot_without_head = ObjectTrace(new_trace_entries)

            out_file.write(str(ot_without_head) + "\n\n")
