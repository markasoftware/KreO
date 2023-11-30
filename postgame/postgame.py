import hashlib
import json
import logging
import sys
import time
import uuid
from collections import defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable, Generator, cast

import pygtrie  # pyright: ignore[reportMissingTypeStubs]

import postgame.analysis_results as ar
import postgame.parse_object_trace as parse_object_trace
from parseconfig import AnalysisTool
from postgame.method import Method
from postgame.method_store import MethodStore
from postgame.object_trace import ObjectTrace, TraceEntry
from postgame.static_trace import StaticTrace, StaticTraceEntry
from postgame.trie import Node, Trie

SCRIPT_PATH = Path(__file__).parent.absolute()

sys.path.append(str(SCRIPT_PATH / ".."))

from parseconfig import Config  # noqa: E402

LOGGER = logging.getLogger(__name__)
LOGGER.setLevel(logging.DEBUG)


# copied from https://stackoverflow.com/a/3431838/1233320
def md5_file(fname: Path):
    hash_md5 = hashlib.md5()
    try:
        with fname.open("rb") as f:
            for chunk in iter(lambda: f.read(4096), b""):
                hash_md5.update(chunk)
        return hash_md5.hexdigest()
    except FileNotFoundError:
        LOGGER.error("Could not find exe to generate md5")
        return "na"


# =============================================================================
@dataclass
class KreoClass:
    """
    A trie value.

    Attributes:
        name: Class name. The address of the constructor associated with the class.
    """

    tail_returns: list[TraceEntry]

    _uuid: str = str(uuid.uuid4())

    def __str__(self) -> str:
        # the address associated with this class is the hex address of
        # the last element in the tail, representing the destructor
        # associated with this class
        return (
            "KreoClass-"
            + self._uuid[0:5]
            + "@"
            + hex(self.tail_returns[-1].method.address)
        )

    def __hash__(self) -> int:
        return hash(self._uuid)

    @staticmethod
    def to_trace(tail_returns: list[TraceEntry]) -> list[int]:
        return [x.method.address for x in tail_returns]

    @staticmethod
    def from_trace(trace: str) -> list[str]:
        return trace.split("/")


# =============================================================================
class TriePrinter:
    def __init__(
        self,
        kreo_class_to_method_set_map: dict[KreoClass, set[Method]],
        trie: pygtrie.Trie,
    ):
        self.__indent = ""
        self.__class_to_method_set = kreo_class_to_method_set_map
        trie.traverse(self.print_trie)  # pyright: ignore[reportUnknownMemberType]

    def print_trie(
        self,
        path_conv: Callable[[tuple[str, ...]], str],
        path: tuple[str, ...],
        children: Generator[Any, None, None],
        cls: KreoClass | None = None,
    ) -> None:
        if cls is None:
            LOGGER.info(f"{self.__indent}n/a")
        else:
            LOGGER.info("%s%s", self.__indent, path_conv(path))
            if cls in self.__class_to_method_set:
                for method in sorted(
                    self.__class_to_method_set[cls],
                    key=lambda x: x.type,
                ):
                    LOGGER.info(
                        "%s* %s | %s (%s, %s, %s)",
                        self.__indent,
                        method,
                        method.type,
                        method.seen_in_head,
                        method.seen_in_tail,
                        method.seen_count,
                    )

        self.__indent += "    "
        list(children)
        self.__indent = self.__indent[0:-4]


# =============================================================================
class Postgame:
    def __init__(self, cfg: Config):
        self.__cfg = cfg

        self.traces: set[ObjectTrace] = set()
        self.static_traces: set[StaticTrace] = set()
        self.method_store = MethodStore()

        self.trie = Trie[KreoClass]()

        # we need a way to know which trie nodes correspond to each method, so
        # that if a method gets mapped to multiple places we can reassign it to
        # the LCA.
        self.method_to_class_map: dict[Method, set[KreoClass]] = defaultdict(set)

        # kreo_class_to_method_set_map does not need to be maintained manually -- it
        # will be populated all at once during the map_trie_nodes_to_methods step.
        self.class_to_method_set: dict[KreoClass, set[Method]] = defaultdict(set)

        self.method_candidate_addresses: set[int] = set()

    def run_step(
        self,
        function: Callable[[], None],
        start_msg: str,
        end_msg: str,
    ) -> None:
        """
        Runs the given step function, eprinting start/end messages and profiling the
        step's time.
        """
        LOGGER.info(start_msg)
        start_time = time.perf_counter()
        function()
        end_time = time.perf_counter()
        LOGGER.info("%s (%.2fs)", end_msg, end_time - start_time)

    def parse_input(self):
        self.base_offset, self.traces = parse_object_trace.parse_input(
            self.__cfg,
            self.method_store,
        )

        # parse method names
        for line in Path(str(self.__cfg.object_traces_path) + "-name-map").open():
            addr, name = line.strip().split(" ", 1)
            self.method_store.insert_method_name(int(addr, 16), name)

    def parse_static_traces(self):
        cur_trace: list[StaticTraceEntry] = []

        def flush_cur_trace():
            if len(cur_trace) > 0:
                self.static_traces.add(StaticTrace(cur_trace))

        for line in self.__cfg.static_traces_path.open():
            if line == "\n":
                flush_cur_trace()
            elif line[0] != "#":
                addr = int(line.split()[0], 16)
                if addr in self.method_candidate_addresses:
                    cur_trace.append(
                        StaticTraceEntry(line, self.method_store.find_or_insert_method)
                    )

        flush_cur_trace()

    def update_all_method_statistics(self):
        self.method_store.reset_all_method_statistics()
        for trace in self.traces:
            trace.update_method_statistics()

    def split_dynamic_traces(self):
        self.__identify_initializer_finalizers()

        new_traces: set[ObjectTrace] = set()

        for trace in self.traces:
            split_trace = trace.split()
            if split_trace:
                new_traces.update(split_trace)
            else:
                new_traces.add(trace)

        self.traces = new_traces

    def split_static_traces(self):
        self.static_traces = set(
            [
                splitted_trace
                for trace in self.static_traces
                for splitted_trace in trace.split()
            ]
        )

    def discover_methods_statically(self):
        """
        Uses the data from the static traces to discover new methods and assign
        them to the appropriate class. Does not update how dynamically-detected
        methods are assigned, because their destructor fingerprints usually
        provide more information about hierarchy than we can hope to glean from
        such a simple static analysis (we just assign things to the LCA of all
        the classes of all dynamically detected methods in the traces).
        """
        # In reality, this routine is only called after the first
        # reorganization, so every dynamically detected method is already
        # assigned to a single class, not a set, but we write assuming it is
        # assigned to a set anyway to be safe (e.g., no next(iter(...)))
        for static_trace in self.static_traces:
            trace_entries = set(static_trace.entries)

            dynamic_entries: set[StaticTraceEntry] = set()
            for entry in trace_entries:
                meth = self.method_store.get_method(entry.method.address)
                if meth is not None and meth.found_dynamically:
                    dynamic_entries.add(entry)

            static_entries = trace_entries - dynamic_entries

            # Map statically found methods to classes containing dynamic methods
            # in the same trace.
            for static_entry in static_entries:
                static_method = static_entry.method
                for dynamic_entry in dynamic_entries:
                    dynamic_method = dynamic_entry.method

                    if dynamic_method in self.method_to_class_map:
                        self.method_to_class_map[static_method].update(
                            self.method_to_class_map[dynamic_method]
                        )

    def construct_trie(self):
        for ot in self.traces:
            tail_returns = ot.tail_returns()

            # Insert class and any parents into the trie using the trace's tail
            for i in range(len(tail_returns)):
                self.trie.insert_value(
                    KreoClass.to_trace(tail_returns[: i + 1]),
                    KreoClass(tail_returns[: i + 1]),
                )

            # Map all methods in the trace to the class in the trie
            cls = self.get_cls(self.trie.get_node(KreoClass.to_trace(tail_returns)))
            for method in ot.methods():
                self.method_to_class_map[method].add(cls)

    def __trie_lca(self, classes: set[KreoClass]) -> KreoClass | None:
        """
        Finds the least common ancestor between the set of classes. The LCA is a
        KreoClass.
        """
        class_traces: list[list[TraceEntry]] = [cls.tail_returns for cls in classes]

        i = 0
        while True:
            try:
                same = all(
                    [
                        class_traces[0][i].method.address == x[i].method.address
                        for x in class_traces
                    ]
                )
            except IndexError:
                break
            if not same:
                break

            i += 1

        shared_trace = class_traces[0][:i]

        if shared_trace == []:
            return None

        return cast(
            "Node[KreoClass]", self.trie.get_node(KreoClass.to_trace(shared_trace))
        ).value

    def update_tail_returns(self, root_return: TraceEntry, node: Node[KreoClass]):
        for child in node.children.values():
            if child.value:
                child.value.tail_returns = [root_return] + child.value.tail_returns
            self.update_tail_returns(root_return, child)

    def swim_methods_in_multiple_classes(self):
        # ensure each method is associated with exactly one class by swimming
        # methods to higher trie nodes.
        for method, cls_set in self.method_to_class_map.items():
            if len(cls_set) == 1:
                # Don't have to reorganize if the method already belongs to exactly one class.
                continue

            # the method belongs to multiple classes, find the LCA

            lca = self.__trie_lca(cls_set)

            if lca:
                LOGGER.debug(f"Found LCA for method {method}, LCA = {lca}")
                # LCA exists
                self.method_to_class_map[method] = set([lca])
            else:
                # LCA doesn't exist, have to add class to trie

                """
                Create a new trie node, placing the classes as children in the new node
                and insert this node into the trie
                
                Remove from trie all classes that are being moved and update each
                class's tail, then reinsert into trie.

                This includes superclasses of the classes in question. In particular,
                consider the following trie before re-organization:

                root
                 / \
                /   \
                a   b
                |   | \
                c   d  e

                If d and c share the same method, the LCA is the root and we must create
                a new node. While this is not necessarily accurate, resolve this issue
                by placing c and d along with all the classes belonging to the same branch
                of the tree under the newly created node:
                
                root
                  |
                 new
                 / \
                /   \
                a   b
                |   | \
                c   d  e
                """

                # TODO what happens if the method is in a trace already?

                new_cls_te = TraceEntry(method, False)
                new_cls_node = Node(method.address, KreoClass([new_cls_te]))

                traces: list[list[int]] = []

                #
                for cls in cls_set:
                    traces.append(KreoClass.to_trace(cls.tail_returns)[:1])
                    try:
                        node = self.trie.get_node(traces[-1])
                        if node is None:
                            raise RuntimeError()
                        # remove node from trie and add to dict
                        self.trie.remove_node(traces[-1])
                        new_cls_node.children[node.address] = node
                    except:
                        # already removed
                        pass

                # update each KreoClass's tail returns to reflect the updated trie.
                self.update_tail_returns(new_cls_te, new_cls_node)

                self.trie.insert_node([method.address], new_cls_node)
                LOGGER.debug(
                    f"Failed to find LCA for method {method.address}, adding node with address {new_cls_node.address} to trie. Placing nodes with base traces under new node: {traces}"
                )

                # Move method to new class in methodToKreoClassMap
                self.method_to_class_map[method] = set(
                    [cast("KreoClass", new_cls_node.value)]
                )

        # Each method is now associated with exactly one class
        for cls_set in self.method_to_class_map.values():
            assert len(cls_set) == 1

    def get_cls(self, node: Node[KreoClass] | None) -> KreoClass:
        assert node is not None
        cls = node.value
        assert cls is not None
        return cls

    def swim_destructors(self):
        # Move destructor functions into correct location in the trie. There is the
        # possibility that a parent object was never constructed but a child was. In
        # this case we know the destructor belongs to the parent but it currently
        # belongs to the child.
        for ot in self.traces:
            tail_returns = ot.tail_returns()

            base_cls = self.get_cls(
                self.trie.get_node(KreoClass.to_trace(tail_returns))
            )

            for i in range(len(tail_returns)):
                cls = self.get_cls(
                    self.trie.get_node(KreoClass.to_trace(tail_returns[: i + 1]))
                )

                tail_return_method = tail_returns[i].method
                if base_cls in self.method_to_class_map[tail_return_method]:
                    self.method_to_class_map[tail_return_method].remove(base_cls)
                self.method_to_class_map[tail_return_method].add(cls)

    def swim_constructors(self):
        """
        Move constructors into correct location in the trie. This only works when
        the head and tail have the same number of items. This allows us to match
        destructors and constructors up, and move the constructor belonging to
        the same class as the matching destructor into the correct place in the
        trie.
        """
        for ot in self.traces:
            head_calls = ot.head_calls()
            tail_returns = ot.tail_returns()

            cls = self.get_cls(self.trie.get_node(KreoClass.to_trace(tail_returns)))

            for i in range(len(tail_returns)):
                # class that is a parent of cls
                parent_cls = self.get_cls(
                    self.trie.get_node(KreoClass.to_trace(tail_returns[: i + 1]))
                )
                # head call belonging to the parent cls
                head_call_method = head_calls[i].method
                # head method is associated with the class; remove it from this class
                # and add to parent class
                if cls in self.method_to_class_map[head_call_method]:
                    self.method_to_class_map[head_call_method].remove(cls)
                self.method_to_class_map[head_call_method].add(parent_cls)

    @staticmethod
    def __map_head_methods_to_traces(
        head_calls: list[TraceEntry],
        head: list[TraceEntry],
        head_calls_to_traces: dict[Method, list[int]],
    ) -> dict[Method, list[int]]:
        meth_to_dtor: dict[Method, list[int]] = {}
        for te in head[len(head_calls) :]:
            if te.is_call:
                meth_to_dtor[te.method] = head_calls_to_traces[head_calls[-1].method]
            elif head_calls[-1].method == te.method:
                head_calls = head_calls[:-1]
        return meth_to_dtor

    @staticmethod
    def __map_tail_methods_to_traces(
        tail_returns: list[TraceEntry],
        tail: list[TraceEntry],
        tail_returns_to_trace: dict[Method, list[int]],
    ) -> dict[Method, list[int]]:
        meth_to_dtor: dict[Method, list[int]] = {}
        for te in reversed(tail[: -len(tail_returns)]):
            if not te.is_call:
                meth_to_dtor[te.method] = tail_returns_to_trace[tail_returns[-1].method]
            elif tail_returns[-1].method == te.method:
                tail_returns = tail_returns[:-1]
        return meth_to_dtor

    def swim_methods_called_in_ctors_and_dtors(self):
        for ot in self.traces:
            head_calls = ot.head_calls()
            tail_returns = ot.tail_returns()
            head = ot.head()
            tail = ot.tail()

            head_calls_to_traces: dict[Method, list[int]] = {}
            tail_returns_to_trace: dict[Method, list[int]] = {}
            for i in range(len(tail_returns)):
                trace = KreoClass.to_trace(tail_returns[: i + 1])
                head_calls_to_traces[head_calls[i].method] = trace
                tail_returns_to_trace[tail_returns[i].method] = trace

            method_to_trace_map: dict[Method, list[int]] = {}

            method_to_trace_map.update(
                Postgame.__map_head_methods_to_traces(
                    head_calls,
                    head,
                    head_calls_to_traces,
                ),
            )

            method_to_trace_map.update(
                Postgame.__map_tail_methods_to_traces(
                    tail_returns,
                    tail,
                    tail_returns_to_trace,
                ),
            )

            # remove method from current class and add to appropriate parent as required.
            cls = self.get_cls(self.trie.get_node(KreoClass.to_trace(tail_returns)))
            for method, trace in method_to_trace_map.items():
                parent_cls = self.get_cls(self.trie.get_node(trace))
                if cls in self.method_to_class_map[method]:
                    self.method_to_class_map[method].remove(cls)
                self.method_to_class_map[method].add(parent_cls)

    def map_trie_nodes_to_methods(self):
        # map trie nodes to methods now that method locations are fixed
        for method, cls_set in self.method_to_class_map.items():
            if len(cls_set) > 1:
                LOGGER.info(
                    "Method mapped to multiple classes: %s (cls tails = %s)",
                    method,
                    [[hex(y.method.address) for y in x.tail_returns] for x in cls_set],
                )
            elif len(cls_set) == 0:
                LOGGER.fatal("Method not mapped to any classes: %s", method)

            cls = next(iter(cls_set))

            self.class_to_method_set[cls].add(method)

    def __generate_json(
        self,
        analysis_results: ar.AnalysisResults,
        parent_node: Node[KreoClass],
        node: Node[KreoClass],
    ):
        if node.value:
            analysis_results.structures[str(node.value)] = ar.Structure(
                name=str(node.value)
            )

            if parent_node.value:
                analysis_results.structures[str(node.value)].members["0x0"] = ar.Member(
                    name=str(parent_node.value) + "_0x0",
                    struc=str(parent_node.value),
                )

            # If there are no methods associated with the trie node there might not
            # be any methods in the set
            if node.value in self.class_to_method_set:
                for method in self.class_to_method_set[node.value]:
                    method_addr_str = hex(method.address + self.base_offset)
                    analysis_results.structures[str(node.value)].methods[
                        method_addr_str
                    ] = ar.Method(
                        demangled_name=method.name if method.name else "",
                        ea=method_addr_str,
                        name=method.type + "_" + method_addr_str,
                        type=method.type,
                    )

        for child in node.children.values():
            self.__generate_json(analysis_results, node, child)

    def generate_json(self):
        # Output json in OOAnalyzer format

        final_json = ar.AnalysisResults(
            filename=self.__cfg.binary_path.name,
            filemd5=md5_file(self.__cfg.binary_path),
            version="kreo-0.1.0",
        )

        for child in self.trie.root.children.values():
            self.__generate_json(final_json, self.trie.root, child)

        with self.__cfg.results_json.open("w") as f:
            f.write(json.dumps(final_json.model_dump(), indent=4))

    def load_method_candidates(self) -> None:
        for line in self.__cfg.method_candidates_path.open():
            self.method_candidate_addresses.add(int(line, 16))

    def __identify_initializer_finalizers(self) -> None:
        for ot in self.traces:
            ot.identify_initializer_finalizer()

    def __update_head_tail(self) -> None:
        for ot in self.traces:
            ot.update_head_tail()

    def remove_ots_with_no_tail(self) -> None:
        self.__update_head_tail()

        self.traces = set([ot for ot in self.traces if ot.tail_returns() != []])

    def update_method_type(self) -> None:
        for meth in self.method_store.get_methods():
            meth.update_type()

    def analysis_tool_lego(self) -> bool:
        return self.__cfg.analysis_tool == AnalysisTool.LEGO

    def analysis_tool_lego_plus(self) -> bool:
        return self.__cfg.analysis_tool == AnalysisTool.LEGO_PLUS

    def analysis_tool_kreo(self) -> bool:
        return self.__cfg.analysis_tool == AnalysisTool.KREO

    def main(self):
        self.run_step(self.parse_input, "parsing input...", "input parsed")
        LOGGER.info("Found %i traces", len(self.traces))

        # self.run_step(
        #     self.split_dynamic_traces,
        #     "splitting traces...",
        #     "traces split",
        # )
        LOGGER.info("after splitting there are now %i traces", len(self.traces))

        if not self.analysis_tool_lego():
            self.run_step(
                self.remove_ots_with_no_tail,
                "removing object traces with no tail...",
                "object traces with no tail removed",
            )

        self.run_step(
            self.update_all_method_statistics,
            "updating method statistics...",
            "method statistics updated",
        )

        self.run_step(
            self.update_method_type,
            "updating method type...",
            "method type removed",
        )

        self.run_step(
            self.construct_trie,
            "constructing trie...",
            "trie constructed",
        )

        self.run_step(
            self.swim_destructors,
            "moving destructors up in trie...",
            "destructors moved up",
        )

        if not self.analysis_tool_lego():
            self.run_step(
                self.swim_constructors,
                "moving constructors up in the trie...",
                "constructors moved up",
            )

            self.run_step(
                self.swim_methods_called_in_ctors_and_dtors,
                "moving methods called in ctors and dtors up in the trie...",
                "methods moved up",
            )

        self.run_step(
            self.swim_methods_in_multiple_classes,
            "reorganizing trie...",
            "trie reorganized",
        )

        if self.analysis_tool_kreo():
            self.run_step(
                self.load_method_candidates,
                "loading method candidates...",
                "method candidates loaded",
            )

            self.run_step(
                self.parse_static_traces,
                "parsing static traces...",
                "static traces parsed",
            )
            self.run_step(
                self.split_static_traces,
                "splitting static traces...",
                "static traces split",
            )
            self.run_step(
                self.discover_methods_statically,
                "discovering methods from static traces...",
                "static methods discovered",
            )

            # discover_methods_statically leaves the new methods assigned to sets of
            # classes still

            # Don't reorganize the trie based on statically discovered methods
            # due to low precision of the method being assigned to the correct class.
            # self.run_step(self.reorganize_trie, '2nd reorganizing trie...', '2nd trie
            # reorganization complete')

        self.run_step(
            self.update_all_method_statistics,
            "updating method statistics...",
            "method statistics updated",
        )

        self.run_step(
            self.update_method_type,
            "updating method type...",
            "method type updated",
        )

        self.run_step(
            self.map_trie_nodes_to_methods,
            "mapping trie nodes to methods...",
            "trie nodes mapped",
        )

        self.run_step(
            self.generate_json,
            "generating json...",
            "json generated",
        )

        print(self.trie)

        LOGGER.info("Done, Kreo exiting normally.")
