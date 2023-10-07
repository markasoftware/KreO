# from postgame.object_trace import TE

from postgame.method import Method
from postgame.object_trace import ObjectTrace as OT
from postgame.object_trace import TraceEntry as TE


def test_trace_entry_construct():
    _ = TE(Method(0x0), False)


def test_trace_entry_str():
    te = TE(Method(0xFF), False)
    assert "ff" == str(te)
    te = TE(Method(0xEE), True)
    assert "ee 1" == str(te)


def test_trace_entry_eq():
    te1 = TE(Method(0xFF), False)
    te2 = TE(Method(0xFF), False)
    te3 = TE(Method(0xFF), True)
    te4 = TE(Method(0xEE), True)

    assert te1 == te1
    assert te1 != te2  # methods are different (compare pointers)
    assert te1 != te3
    assert te1 != te4
    assert te3 != te4


def test_object_trace_construct():
    _ = OT([])


def simple_ot() -> OT:
    methods = [Method(0), Method(1)]
    return OT(
        [
            TE(methods[0], True),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[0], False),
        ]
    )


def test_object_trace_head_tail():
    methods = [Method(0), Method(1)]
    ot = OT(
        [
            TE(methods[0], True),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[0], False),
        ]
    )

    head = ot.head()

    assert 2 == len(head)
    assert methods[0] == head[0].method
    assert methods[1] == head[1].method

    tail = ot.tail()

    assert 2 == len(tail)
    assert methods[0] == tail[0].method
    assert methods[1] == tail[1].method


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
    ot1 = OT(
        [
            TE(methods[0], True),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[0], False),
        ]
    )
    ot2 = OT(
        [
            TE(methods[0], True),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[0], False),
        ]
    )
    ot3 = OT(
        [
            TE(methods[0], True),
            TE(methods[0], False),
        ]
    )

    assert ot1 == ot2
    assert ot1 != ot3


def test_identify_initializer_finalizer():
    methods = [Method(0), Method(1)]
    ot = OT(
        [
            TE(methods[0], True),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[0], False),
        ]
    )

    ot.identify_initializer_finalizer()

    assert methods[0].is_initializer
    assert methods[0].is_finalizer
    assert not methods[1].is_initializer
    assert not methods[1].is_finalizer


def test_identify_initializer_finalizer_empty():
    ot = OT([])
    ot.identify_initializer_finalizer()


def test_identify_initializer_finalizer_medium():
    methods = [Method(0), Method(1), Method(2)]
    ot = OT(
        [
            TE(methods[0], True),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[0], False),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[2], True),
            TE(methods[2], False),
        ]
    )
    ot.identify_initializer_finalizer()

    assert methods[0].is_initializer
    assert not methods[0].is_finalizer
    assert not methods[1].is_initializer
    assert not methods[1].is_finalizer
    assert not methods[2].is_initializer
    assert methods[2].is_finalizer


def test_methods():
    methods = [Method(0), Method(1), Method(2)]
    ot = OT(
        [
            TE(methods[0], True),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[0], False),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[2], True),
            TE(methods[2], False),
        ]
    )

    meth = ot.methods()
    assert 3 == len(meth)
    assert methods[0] in meth
    assert methods[1] in meth
    assert methods[2] in meth


def test_split_empty():
    ot = OT([])
    ot.identify_initializer_finalizer()
    split_traces = list(ot.split())
    assert [] == split_traces


def test_split_no_split_required():
    ot = simple_ot()
    ot.identify_initializer_finalizer()
    split_traces = list(ot.split())
    assert 1 == len(split_traces)
    assert ot == split_traces[0]


def test_split_when_required():
    methods = [Method(0), Method(1), Method(2)]
    ot = OT(
        [
            TE(methods[0], True),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[0], False),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[2], True),
            TE(methods[2], False),
            #
            TE(methods[0], True),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[0], False),
            TE(methods[2], True),
            TE(methods[2], False),
        ]
    )

    ot.identify_initializer_finalizer()
    split_traces = list(ot.split())
    assert 2 == len(split_traces)
    assert (
        OT(
            [
                TE(methods[0], True),
                TE(methods[1], True),
                TE(methods[1], False),
                TE(methods[0], False),
                TE(methods[1], True),
                TE(methods[1], False),
                TE(methods[2], True),
                TE(methods[2], False),
            ]
        )
        == split_traces[0]
    )
    assert (
        OT(
            [
                TE(methods[0], True),
                TE(methods[1], True),
                TE(methods[1], False),
                TE(methods[0], False),
                TE(methods[2], True),
                TE(methods[2], False),
            ]
        )
        == split_traces[1]
    )


def test_update_head_tail_1():
    methods = [Method(0)]

    ot = OT(
        [
            TE(methods[0], True),
            TE(methods[0], False),
        ]
    )

    ot.update_head_tail()

    head = ot.head()
    tail = ot.tail()

    assert [] == head
    assert [] == tail


def test_update_head_tail_2():
    methods = [Method(0), Method(1)]

    ot = OT(
        [
            TE(methods[0], True),
            TE(methods[0], False),
            TE(methods[1], True),
            TE(methods[1], False),
        ]
    )

    ot.update_head_tail()

    head = [x.method for x in ot.head()]
    tail = [x.method for x in ot.tail()]

    assert [methods[0]] == head
    assert [methods[1]] == tail


def test_update_head_tail_3():
    methods = [Method(0), Method(1), Method(2)]

    ot = OT(
        [
            TE(methods[0], True),
            TE(methods[0], False),
            TE(methods[1], True),
            TE(methods[2], True),
            TE(methods[2], False),
            TE(methods[1], False),
        ]
    )

    ot.update_head_tail()

    head = [x.method for x in ot.head()]
    tail = [x.method for x in ot.tail()]

    assert [methods[0]] == head
    assert [methods[1]] == tail


def test_update_head_tail_4():
    methods = [Method(0), Method(1), Method(2)]

    ot = OT(
        [
            TE(methods[1], True),
            TE(methods[2], True),
            TE(methods[2], False),
            TE(methods[1], False),
            TE(methods[0], True),
            TE(methods[0], False),
        ]
    )

    ot.update_head_tail()

    head = [x.method for x in ot.head()]
    tail = [x.method for x in ot.tail()]

    assert [methods[1]] == head
    assert [methods[0]] == tail


def test_update_method_statistics_1():
    methods = [Method(0), Method(1), Method(2)]

    ot = OT(
        [
            TE(methods[0], True),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[0], False),
            TE(methods[2], True),
            TE(methods[2], False),
        ]
    )

    ot.update_method_statistics()

    assert 1 == methods[0].seen_count
    assert 1 == methods[1].seen_count
    assert 1 == methods[2].seen_count

    assert 1 == methods[0].seen_in_head
    assert 1 == methods[1].seen_in_head
    assert 0 == methods[2].seen_in_head

    assert 0 == methods[0].seen_in_tail
    assert 0 == methods[1].seen_in_tail
    assert 1 == methods[2].seen_in_tail


def test_update_method_statistics_2():
    methods = [Method(0), Method(1), Method(2)]

    ot = OT(
        [
            TE(methods[0], True),
            TE(methods[0], False),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[1], True),
            TE(methods[1], False),
            TE(methods[2], True),
            TE(methods[2], False),
        ]
    )

    ot.update_method_statistics()

    assert 1 == methods[0].seen_count
    assert 2 == methods[1].seen_count
    assert 1 == methods[2].seen_count

    assert 1 == methods[0].seen_in_head
    assert 0 == methods[1].seen_in_head
    assert 0 == methods[2].seen_in_head

    assert 0 == methods[0].seen_in_tail
    assert 0 == methods[1].seen_in_tail
    assert 1 == methods[2].seen_in_tail
