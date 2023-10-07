import hashlib
import json
import logging
import sys
import time
import uuid
from collections import defaultdict
from pathlib import Path
from typing import Any, Callable, Generator, cast

import pygtrie  # pyright: ignore[reportMissingTypeStubs]

import postgame.analysis_results as ar
import postgame.parse_object_trace as parse_object_trace
from postgame.method import Method
from postgame.method_store import MethodStore
from postgame.object_trace import ObjectTrace
from postgame.static_trace import StaticTrace, StaticTraceEntry

SCRIPT_PATH = Path(__file__).parent.absolute()

sys.path.append(str(SCRIPT_PATH / ".."))

from parseconfig import Config  # noqa: E402

LOGGER = logging.getLogger(__name__)
LOGGER.setLevel(logging.INFO)
handler = logging.StreamHandler()
handler.setLevel(logging.INFO)
LOGGER.addHandler(handler)


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
class KreoClass:
    """
    Represents a value in a trie.
    """

    def __init__(self, tail: list[Method]):
        self.uuid = str(uuid.uuid4())
        self.tail = tail
        assert len(self.tail) > 0

    def __str__(self) -> str:
        # the address associated with this class is the hex address of
        # the last element in the tail, representing the destructor
        # associated with this class
        assert len(self.tail) > 0
        return "KreoClass-" + self.uuid[0:5] + "@" + hex(self.tail[-1].address)

    @staticmethod
    def tail_str(tail: list[Method]) -> str:
        fstr = [str(m) for m in tail]
        return "/".join(fstr)


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
            last_tail_method = cls.tail[-1]
            LOGGER.info("%s%s %s", self.__indent, path_conv(path), last_tail_method)
            if cls in self.__class_to_method_set:
                for method in self.__class_to_method_set[cls]:
                    LOGGER.info(f" {self.__indent}* {method} |")

        self.__indent += "    "
        list(children)
        self.__indent = self.__indent[0:-4]


# =============================================================================
class Postgame:
    def __init__(self, cfg: Config):
        self.__cfg = cfg

        self.__traces: set[ObjectTrace] = set()
        self.__static_traces: set[StaticTrace] = set()
        self.__method_store = MethodStore()

        self.__trie = pygtrie.StringTrie()

        # we need a way to know which trie nodes correspond to each method, so
        # that if a method gets mapped to multiple places we can reassign it to
        # the LCA.
        self.__method_to_class: dict[Method, set[KreoClass]] = defaultdict(set)

        # kreo_class_to_method_set_map does not need to be maintained manually -- it
        # will be populated all at once during the map_trie_nodes_to_methods step.
        self.__class_to_method_set: dict[KreoClass, set[Method]] = defaultdict(set)

        self.__method_candidate_addresses: set[int] = set()

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
        self.__base_offset, self.__traces = parse_object_trace.parse_input(
            self.__cfg,
            self.__method_store,
        )

    def parse_method_names(self):
        for line in Path(str(self.__cfg.object_traces_path) + "-name-map").open():
            addr, name = line.split(" ", 1)
            self.__method_store.insert_method_name(int(addr, 16), name)

    def parse_static_traces(self):
        cur_trace: list[StaticTraceEntry] = []

        def flush_cur_trace():
            if len(cur_trace) > 0:
                self.__static_traces.add(StaticTrace(cur_trace))

        for line in self.__cfg.static_traces_path.open():
            if line == "\n":
                flush_cur_trace()
            elif line[0] != "#":
                addr = int(line.split()[0], 16)
                if addr in self.__method_candidate_addresses:
                    cur_trace.append(
                        StaticTraceEntry(
                            line, self.__method_store.find_or_insert_method
                        )
                    )

        flush_cur_trace()

    def update_all_method_statistics(self):
        self.__method_store.reset_all_method_statistics()
        for trace in self.__traces:
            trace.update_method_statistics()

    def split_dynamic_traces(self):
        self.__traces = set(
            [
                splitted_trace
                for trace in self.__traces
                for splitted_trace in trace.split()
            ]
        )

    def split_static_traces(self):
        self.__static_traces = set(
            [
                splitted_trace
                for trace in self.__static_traces
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
        for static_trace in self.__static_traces:
            trace_entries = set(static_trace.entries)

            dynamic_entries: set[StaticTraceEntry] = set()
            for entry in trace_entries:
                meth = self.__method_store.get_method(entry.method.address)
                if meth is not None and meth.found_dynamically:
                    dynamic_entries.add(entry)

            static_entries = trace_entries - dynamic_entries

            # Map statically found methods to classes containing dynamic methods
            # in the same trace.
            for static_entry in static_entries:
                static_method = static_entry.method
                for dynamic_entry in dynamic_entries:
                    dynamic_method = dynamic_entry.method

                    if dynamic_method in self.__method_to_class:
                        self.__method_to_class[static_method].update(
                            self.__method_to_class[dynamic_method]
                        )

    def construct_trie(self):
        for trace in self.__traces:
            tail = [x.method for x in trace.tail()]

            # Insert class and any parents into the trie using the trace's tail
            for i in range(len(tail)):
                partial_tail = tail[0 : i + 1]
                partial_tail_str = KreoClass.tail_str(partial_tail)

                trie_keys = cast(
                    "set[str]",
                    self.__trie.keys(),  # pyright: ignore[reportUnknownMemberType]
                )

                if partial_tail_str not in trie_keys:
                    self.__trie[partial_tail_str] = KreoClass(partial_tail)

            # Map all methods in the trace to the class in the trie
            cls = cast("KreoClass", self.__trie[KreoClass.tail_str(tail)])
            for method in trace.methods():
                self.__method_to_class[method].add(cls)

    def trieLCA(self, classes: set[KreoClass]) -> KreoClass:
        """
        Finds the least common ancestor between the set of classes. The LCA is a KreoClass.
        """

        def findLCA(
            cls_trie_node_lists: list[list[tuple[str, pygtrie._Node]]]
        ) -> list[tuple[str, pygtrie._Node]]:
            shortest_node_list = None
            for node_list in cls_trie_node_lists:
                if shortest_node_list is None or len(shortest_node_list) > len(
                    node_list
                ):
                    shortest_node_list = node_list
            assert shortest_node_list != None

            # Check to see if other classes have identical starting nodes.
            for i in range(len(shortest_node_list)):
                for node_list in cls_trie_node_lists:
                    if node_list[i][0] != shortest_node_list[i][0]:
                        return node_list[:i]

            return shortest_node_list

        cls_trie_node_lists: list[list[tuple[str, pygtrie._Node]]] = [
            self.__trie._get_node(KreoClass.tail_str(cls.tail))[1] for cls in classes
        ]

        shared_node_list = findLCA(cls_trie_node_lists)

        if len(shared_node_list) > 1:
            # Shared class is KreoClass associated with the final node.
            return shared_node_list[-1][1].value
        else:
            return None

    def reorganize_trie(self):
        # ensure each method is associated with exactly one class by swimming
        # methods to higher trie nodes.
        for method, cls_set in self.__method_to_class.items():
            if len(cls_set) == 1:
                # Don't have to reorganize if the method already belongs to exactly one class.
                continue

            lca = self.trieLCA(cls_set)

            if lca:
                # LCA exists
                self.__method_to_class[method] = set([lca])
            else:
                # LCA doesn't exist, have to add class to trie

                """
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

                If d nd c share the same method, the LCA is the root and we must create
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
                new_cls_tail = [method]

                classes_to_update: list[tuple[str, KreoClass]] = list()
                tail_strs: set[str] = set()
                for cls in cls_set:
                    base_cls_tail = KreoClass.tail_str([cls.tail[0]])
                    if base_cls_tail not in tail_strs:
                        tail_strs.add(base_cls_tail)
                        classes_to_update += self.__trie.items(prefix=base_cls_tail)

                for key, cls in classes_to_update:
                    assert self.__trie.has_key(key)
                    if self.__trie.has_key(key):
                        self.__trie.pop(key)
                        cls.tail = new_cls_tail + cls.tail
                        self.__trie[KreoClass.tail_str(cls.tail)] = cls

                # Create class that will be inserted into the trie. This
                # class must have a new tail that is the method
                # that will be assigned to the class that the two
                # classes share.
                new_cls = KreoClass(new_cls_tail)

                self.__trie[KreoClass.tail_str(new_cls_tail)] = new_cls

                # Move method to new class in methodToKreoClassMap
                self.__method_to_class[method] = set([new_cls])

        # Each method is now associated with exactly one class
        for cls_set in self.__method_to_class.values():
            assert len(cls_set) == 1

    def swim_destructors(self):
        # Move destructor functions into correct location in the trie. There is the
        # possibility that a parent object was never constructed but a child was. In
        # this case we know the destructor belongs to the parent but it currently
        # belongs to the child.
        for key, node in self.__trie.items():
            # Find tail[-1] in _method_to_class and replace the reference
            # with this class
            destructor = node.tail[-1]
            if destructor in self.__method_to_class:
                cls_set = set([node])
                if cls_set != self.__method_to_class[destructor]:
                    self.__method_to_class[destructor] = cls_set

    def map_trie_nodes_to_methods(self):
        # map trie nodes to methods now that method locations are fixed
        for method, trie_node in self.__method_to_class.items():
            if len(trie_node) > 1:
                LOGGER.info("Method mapped to multiple classes %s", method)
            trie_node = list(trie_node)[0]
            self.__class_to_method_set[trie_node].add(method)

    def generate_json(self):
        # Output json in OOAnalyzer format

        final_json = ar.AnalysisResults(
            filename=self.__cfg.binary_path.name,
            filemd5=md5_file(self.__cfg.binary_path),
            version="kreo-0.1.0",
        )

        for name, cls in cast("dict[str, KreoClass]", self.__trie).items():
            trace = name.split("/")
            parent_cls: KreoClass | None = None
            if len(trace) > 2:
                parent_cls = cast("KreoClass", self.__trie["/".join(trace[:-2])])

            final_json.structures[str(cls)] = ar.Structure(name=str(cls))

            # For now, while we only detect direct parent relationships, only
            # add a member if we have a parent, and don't actually know anything
            # about its size
            if parent_cls is not None:
                final_json.structures[str(cls)].members["0x0"] = ar.Member(
                    name=str(parent_cls) + "_0x0",
                    struc=str(parent_cls),
                )

            # If there are no methods associated with the trie node there might not
            # be any methods in the set
            if cls in self.__class_to_method_set:
                for method in self.__class_to_method_set[cls]:
                    method_addr_str = hex(method.address + self.__base_offset)
                    final_json.structures[str(cls)].methods[
                        method_addr_str
                    ] = ar.Method(
                        demangled_name=method.name if method.name else "",
                        ea=method_addr_str,
                        name=method.type + "_" + method_addr_str,
                        type=method.type,
                    )

        with self.__cfg.results_json.open("w") as f:
            f.write(json.dumps(final_json.dict(), indent=4))

    def load_method_candidates(self) -> None:
        for line in self.__cfg.method_candidates_path.open():
            self.__method_candidate_addresses.add(int(line, 16))

    def identify_initializer_finalizers(self) -> None:
        for ot in self.__traces:
            ot.identify_initializer_finalizer()

    def update_head_tail(self) -> None:
        for ot in self.__traces:
            ot.update_head_tail()

    def remove_ots_with_no_tail(self) -> None:
        self.__traces = set([ot for ot in self.__traces if ot.tail() != []])

    def main(self):
        self.run_step(self.parse_input, "parsing input...", "input parsed")
        LOGGER.info("Found %i traces", len(self.__traces))

        self.run_step(
            self.identify_initializer_finalizers,
            "identifying initializers and finalizers...",
            "initializers and finalizers identified",
        )

        self.run_step(
            self.split_dynamic_traces,
            "splitting traces...",
            "traces split",
        )
        LOGGER.info("after splitting there are now %i traces", len(self.__traces))

        self.run_step(
            self.update_head_tail,
            "updating head and tail...",
            "head and tail updated",
        )

        self.run_step(
            self.remove_ots_with_no_tail,
            "removing object traces with no tail...",
            "object traces with no tail removed",
        )

        self.run_step(
            self.construct_trie,
            "constructing trie...",
            "trie constructed",
        )

        self.run_step(
            self.reorganize_trie,
            "reorganizing trie...",
            "trie reorganized",
        )

        self.run_step(
            self.swim_destructors,
            "moving destructors up in trie...",
            "destructors moved up",
        )

        if self.__cfg.enable_static_alias_analysis:
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
            self.parse_method_names,
            "parsing method names...",
            "method names parsed",
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

        TriePrinter(self.__class_to_method_set, self.__trie)

        LOGGER.info("Done, Kreo exiting normally.")
