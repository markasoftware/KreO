# from postgame.object_trace import TraceEntry

import sys
from pathlib import Path

sys.path.append(str(Path(__file__).parent / ".."))
sys.path.append(str(Path(__file__).parent / ".." / "postgame"))
from postgame.method import Method  # noqa: E402
from postgame.object_trace import ObjectTrace, TraceEntry  # noqa: E402


def test_trace_entry_construct():
    _ = TraceEntry(Method(0x0), False)


def test_trace_entry_str():
    te = TraceEntry(Method(0xFF), False)
    assert "ff" == str(te)
    te = TraceEntry(Method(0xEE), True)
    assert "ee 1" == str(te)


def test_trace_entry_eq():
    te1 = TraceEntry(Method(0xFF), False)
    te2 = TraceEntry(Method(0xFF), False)
    te3 = TraceEntry(Method(0xFF), True)
    te4 = TraceEntry(Method(0xEE), True)

    assert te1 == te1
    assert te1 != te2  # methods are different (compare pointers)
    assert te1 != te3
    assert te1 != te4
    assert te3 != te4


def test_object_trace_construct():
    _ = ObjectTrace([])


def simple_ot() -> ObjectTrace:
    methods = [Method(0), Method(1)]
    return ObjectTrace(
        [
            TraceEntry(methods[0], True),
            TraceEntry(methods[1], True),
            TraceEntry(methods[1], False),
            TraceEntry(methods[0], False),
        ]
    )


def test_object_trace_head_tail():
    methods = [Method(0), Method(1)]
    ot = ObjectTrace(
        [
            TraceEntry(methods[0], True),
            TraceEntry(methods[1], True),
            TraceEntry(methods[1], False),
            TraceEntry(methods[0], False),
        ]
    )

    head = ot.get_head()

    assert 4 == len(head)
    assert methods[0] == head[0].method
    assert methods[1] == head[1].method
    assert methods[1] == head[2].method
    assert methods[0] == head[3].method

    tail = ot.get_tail()

    assert 4 == len(tail)
    assert methods[0] == tail[0].method
    assert methods[1] == tail[1].method
    assert methods[1] == tail[2].method
    assert methods[0] == tail[3].method


def test_object_trace_str():
    ot = str(simple_ot())

    assert (
        """0 1
1 1
1
0"""
        == ot
    )


def test_object_trace_eq():
    methods = [Method(0), Method(1)]
    ot1 = ObjectTrace(
        [
            TraceEntry(methods[0], True),
            TraceEntry(methods[1], True),
            TraceEntry(methods[1], False),
            TraceEntry(methods[0], False),
        ]
    )
    ot2 = ObjectTrace(
        [
            TraceEntry(methods[0], True),
            TraceEntry(methods[1], True),
            TraceEntry(methods[1], False),
            TraceEntry(methods[0], False),
        ]
    )
    ot3 = ObjectTrace(
        [
            TraceEntry(methods[0], True),
            TraceEntry(methods[0], False),
        ]
    )

    assert ot1 == ot2
    assert ot1 != ot3


def test_update_method_stats_basic():
    methods = [Method(0), Method(1)]
    ot = ObjectTrace(
        [
            TraceEntry(methods[0], True),
            TraceEntry(methods[1], True),
            TraceEntry(methods[1], False),
            TraceEntry(methods[0], False),
        ]
    )

    ot.update_method_statistics()

    # assert 1 == methods[0].seen_in_head
    # assert 1 == methods[0].seen_in_tail
    # assert 1 == methods[1].seen_in_head
    # assert 1 == methods[1].seen_in_tail
    # assert methods[0].is_initializer
    # assert methods[0].is_finalizer
    # assert not methods[1].is_initializer
    # assert not methods[1].is_finalizer


def test_update_method_stats_empty():
    ot = ObjectTrace([])
    ot.update_method_statistics()


def test_update_method_stats_medium():
    methods = [Method(0), Method(1), Method(2)]
    ot = ObjectTrace(
        [
            TraceEntry(methods[0], True),
            TraceEntry(methods[1], True),
            TraceEntry(methods[1], False),
            TraceEntry(methods[0], False),
            TraceEntry(methods[1], True),
            TraceEntry(methods[1], False),
            TraceEntry(methods[2], True),
            TraceEntry(methods[2], False),
        ]
    )

    ot.update_method_statistics()

    assert 1 == methods[0].seen_in_head
    assert 0 == methods[0].seen_in_tail
    assert 1 == methods[1].seen_in_head
    assert 0 == methods[1].seen_in_tail
    assert 0 == methods[2].seen_in_head
    assert 1 == methods[2].seen_in_tail

    assert methods[0].is_initializer
    assert not methods[0].is_finalizer
    assert not methods[1].is_initializer
    assert not methods[1].is_finalizer
    assert not methods[2].is_initializer
    assert methods[2].is_finalizer


def test_methods():
    methods = [Method(0), Method(1), Method(2)]
    ot = ObjectTrace(
        [
            TraceEntry(methods[0], True),
            TraceEntry(methods[1], True),
            TraceEntry(methods[1], False),
            TraceEntry(methods[0], False),
            TraceEntry(methods[1], True),
            TraceEntry(methods[1], False),
            TraceEntry(methods[2], True),
            TraceEntry(methods[2], False),
        ]
    )

    meth = set(ot.methods())
    assert 3 == len(meth)
    assert set(methods) == meth


def test_split_empty():
    ot = ObjectTrace([])
    ot.update_method_statistics()
    split_traces = list(ot.split())
    assert [] == split_traces


def test_split_no_split_required():
    ot = simple_ot()
    ot.update_method_statistics()
    split_traces = list(ot.split())
    assert 1 == len(split_traces)
    assert ot == split_traces[0]


def test_split_when_required():
    methods = [Method(0), Method(1), Method(2)]
    ot = ObjectTrace(
        [
            TraceEntry(methods[0], True),
            TraceEntry(methods[1], True),
            TraceEntry(methods[1], False),
            TraceEntry(methods[0], False),
            TraceEntry(methods[1], True),
            TraceEntry(methods[1], False),
            TraceEntry(methods[2], True),
            TraceEntry(methods[2], False),
            #
            TraceEntry(methods[0], True),
            TraceEntry(methods[1], True),
            TraceEntry(methods[1], False),
            TraceEntry(methods[0], False),
            TraceEntry(methods[2], True),
            TraceEntry(methods[2], False),
        ]
    )

    ot.update_method_statistics()
    split_traces = list(ot.split())
    assert 2 == len(split_traces)
    assert (
        ObjectTrace(
            [
                TraceEntry(methods[0], True),
                TraceEntry(methods[1], True),
                TraceEntry(methods[1], False),
                TraceEntry(methods[0], False),
                TraceEntry(methods[1], True),
                TraceEntry(methods[1], False),
                TraceEntry(methods[2], True),
                TraceEntry(methods[2], False),
            ]
        )
        == split_traces[0]
    )
    assert (
        ObjectTrace(
            [
                TraceEntry(methods[0], True),
                TraceEntry(methods[1], True),
                TraceEntry(methods[1], False),
                TraceEntry(methods[0], False),
                TraceEntry(methods[2], True),
                TraceEntry(methods[2], False),
            ]
        )
        == split_traces[1]
    )
