from pathlib import Path

from parseconfig import AnalysisTool, Config
from postgame.postgame import Postgame

SCRIPT_PATH = Path(__file__)
LEGO_CFG = Config(
    config_fname=SCRIPT_PATH,
    analysis_tool=AnalysisTool.LEGO,
    base_directory=Path("data"),
    results_path=Path(),
    results_instrumented_path=Path(),
    pdb_file=Path(),
    binary_path=Path(),
)


def test_postgame_ctor():
    Postgame(LEGO_CFG)


def test_postgame_parse_input():
    dut = Postgame(LEGO_CFG)
    dut.parse_input()
    methods = dut.method_store.get_methods()
    object_traces = dut.traces

    assert 0x400000 == dut.base_offset
    assert len(methods) == 21
    assert len(object_traces) == 6

    meth = dut.method_store.get_method(int("128f0", 16))
    assert meth is not None
    assert "public: void __thiscall baz::one(void)" == meth.name


def test_update_all_method_statistics():
    dut = Postgame(LEGO_CFG)
    dut.parse_input()
    dut.update_all_method_statistics()

    meth1 = dut.method_store.get_method(int("11fa0", 16))
    meth2 = dut.method_store.get_method(int("12620", 16))
    meth3 = dut.method_store.get_method(int("12d40", 16))

    assert meth1 is not None
    assert meth2 is not None
    assert meth3 is not None

    assert meth1.seen_in_head == 1
    assert meth1.seen_count == 1

    assert meth2.seen_in_tail == 1
    assert meth2.seen_count == 1

    assert meth3.seen_in_head == 1
    assert meth3.seen_in_tail == 1
    assert meth3.seen_count == 1
