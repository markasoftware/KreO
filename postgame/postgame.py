import hashlib
import json
import logging
import subprocess
import sys
import time
import uuid
from collections import defaultdict
from pathlib import Path
from typing import Callable

import pygtrie

import postgame.analysis_results as ar
import postgame.parse_object_trace as parse_object_trace
from postgame.method import Method
from postgame.method_store import MethodStore
from postgame.object_trace import ObjectTrace, TraceEntry
from postgame.static_trace import StaticTrace, StaticTraceEntry

SCRIPT_PATH = Path(__file__).parent.absolute()

sys.path.append(str(SCRIPT_PATH / ".."))

from parseconfig import Config  # noqa: E402

LOGGER = logging.getLogger(__name__)


# copied from https://stackoverflow.com/a/3431838/1233320
def md5File(fname: Path):
    hash_md5 = hashlib.md5()
    try:
        with fname.open("rb") as f:
            for chunk in iter(lambda: f.read(4096), b""):
                hash_md5.update(chunk)
        return hash_md5.hexdigest()
    except FileNotFoundError:
        LOGGER.error("Could not find exe to generate md5")
        return "na"


def printTraces(traces: list[ObjectTrace]):
    for trace in traces:
        print(trace)


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
    def tail_str(tail: list[TraceEntry]) -> str:
        fstr = [str(f.method) for f in tail]
        return "/".join(fstr)


# =============================================================================
class TriePrinter:
    def __init__(self, kreo_class_to_method_set_map, trie):
        self._indent = ""
        self._class_to_method_set = kreo_class_to_method_set_map
        trie.traverse(self.printTrie)

    def printTrie(self, pathConv, path, children, cls=None):
        if cls is None:
            print(f"{self._indent}n/a")
        else:
            pathC = pathConv(path)
            lastFingerprintMethod = cls.tail[-1]
            print(
                f"{self._indent}{pathC} {lastFingerprintMethod.is_probably_destructor()} {lastFingerprintMethod.seen_in_head} {lastFingerprintMethod.seen_in_tail} {lastFingerprintMethod.seen_in_torso}"
            )
            if cls in self._class_to_method_set:
                for method in self._class_to_method_set[cls]:
                    method.update_type()
                    print(
                        f" {self._indent}* {method} | {method.seen_in_head} {method.seen_in_tail} {method.seen_in_torso}"
                    )

        self._indent += "    "
        list(children)
        self._indent = self._indent[0:-4]


# =============================================================================
class Postgame:
    def __init__(self, cfg: Config):
        self.cfg = cfg

        self._traces: set[ObjectTrace] = set()
        self._static_traces: set[StaticTrace] = set()
        self._method_store = MethodStore()

        self._trie = pygtrie.StringTrie()

        # we need a way to know which trie nodes correspond to each method, so
        # that if a method gets mapped to multiple places we can reassign it to
        # the LCA.
        self._method_to_class: dict[Method, set[KreoClass]] = defaultdict(set)

        # kreo_class_to_method_set_map does not need to be maintained manually -- it
        # will be populated all at once during the map_trie_nodes_to_methods step.
        self._class_to_method_set: dict[KreoClass, set[Method]] = defaultdict(set)

        self._method_candidate_addresses: set[int] = set()

    def run_step(
        self,
        function: Callable[[], None],
        start_msg: str,
        end_msg: str,
    ) -> None:
        """
        Runs the given step function, eprinting start/end messages and profiling the step's time.
        """
        LOGGER.warning(start_msg)
        start_time = time.perf_counter()
        function()
        end_time = time.perf_counter()
        LOGGER.warning("%s (%.2fs)", end_msg, end_time - start_time)

    def parse_input(self):
        self._base_offset, self._traces = parse_object_trace.parse_input(
            self.cfg,
            self._method_store,
        )

    def parse_method_names(self):
        for line in Path(str(self.cfg.object_traces_path) + "-name-map").open():
            method_addr = int(line[: line.find(" ")], 16)
            mangled_name = line[line.find(" ") + 1 :]
            try:
                p1 = subprocess.Popen(["undname", mangled_name], stdout=subprocess.PIPE)
                assert p1.stdout is not None
                demangled_name = str(p1.stdout.read())
                demangled_name = demangled_name.split("\\n")
                demangled_name = demangled_name[4]
                demangled_name = demangled_name[
                    demangled_name.find('"') + 1 : demangled_name.rfind('"')
                ]
            except FileNotFoundError as _:
                demangled_name = mangled_name
            self._method_store.insertMethodName(method_addr, demangled_name)

    def parse_static_traces(self):
        cur_trace: list[StaticTraceEntry] = []

        def flush_cur_trace():
            if len(cur_trace) > 0:
                self._static_traces.add(StaticTrace(cur_trace))

        for line in self.cfg.static_traces_path.open():
            if line == "\n":
                flush_cur_trace()
            elif line[0] != "#":
                addr = int(line.split()[0], 16)
                if addr in self._method_candidate_addresses:
                    cur_trace.append(
                        StaticTraceEntry(line, self._method_store.find_or_insert_method)
                    )

        flush_cur_trace()

    def update_all_method_statistics(self):
        self._method_store.reset_all_method_statistics()
        for trace in self._traces:
            trace.update_method_statistics()

    def split_dynamic_traces(self):
        self._traces = set(
            [
                splitted_trace
                for trace in self._traces
                for splitted_trace in trace.split()
            ]
        )

    def split_static_traces(self):
        self._static_traces = set(
            [
                splitted_trace
                for trace in self._static_traces
                for splitted_trace in trace.split()
            ]
        )

    def eliminate_object_traces_same_init_finalizer(self):
        new_traces: set[ObjectTrace] = set()
        for trace in self._traces:
            # TODO
            # if trace.head[0] is not trace.tail[-1]:
            new_traces.add(trace)
        self._traces = new_traces

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
        for static_trace in self._static_traces:
            trace_entries = set(static_trace.entries)

            dynamic_entries: set[StaticTraceEntry] = set()
            for entry in trace_entries:
                meth = self._method_store.getMethod(entry.method.address)
                if meth is not None and meth.found_dynamically:
                    dynamic_entries.add(entry)

            static_entries = trace_entries - dynamic_entries

            # Map statically found methods to classes containing dynamic methods
            # in the same trace.
            for static_entry in static_entries:
                static_method = static_entry.method
                for dynamic_entry in dynamic_entries:
                    dynamic_method = dynamic_entry.method

                    if dynamic_method in self._method_to_class:
                        self._method_to_class[static_method].update(
                            self._method_to_class[dynamic_method]
                        )

    def remove_nondestructors_from_fingerprints(self):
        for trace in self._traces:
            trace.tail = [x for x in trace.tail if x.is_probably_destructor()]

        # Remove traces with empty fingerprints
        self._traces = set(filter(lambda trace: len(trace.tail) > 0, self._traces))

    def construct_trie(self):
        for trace in self._traces:
            # Insert class and any parents into the trie using the trace's tail
            for i in range(len(trace.tail)):
                partial_tail = trace.tail[0 : i + 1]
                partial_tail_str = KreoClass.tail_str(partial_tail)
                if partial_tail_str not in self._trie.keys():
                    self._trie[partial_tail_str] = KreoClass(partial_tail)

            # Map all methods in the trace to the class in the trie
            cls = self._trie[KreoClass.tail_str(trace.tail)]
            for method in trace.methods():
                self._method_to_class[method].add(cls)

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
            self._trie._get_node(KreoClass.tail_str(cls.tail))[1] for cls in classes
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
        for method, cls_set in self._method_to_class.items():
            if len(cls_set) == 1:
                # Don't have to reorganize if the method already belongs to exactly one class.
                continue

            lca = self.trieLCA(cls_set)

            if lca:
                # LCA exists
                self._method_to_class[method] = set([lca])
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
                        classes_to_update += self._trie.items(prefix=base_cls_tail)

                for key, cls in classes_to_update:
                    assert self._trie.has_key(key)
                    if self._trie.has_key(key):
                        self._trie.pop(key)
                        cls.tail = new_cls_tail + cls.tail
                        self._trie[KreoClass.tail_str(cls.tail)] = cls

                # Create class that will be inserted into the trie. This
                # class must have a new tail that is the method
                # that will be assigned to the class that the two
                # classes share.
                new_cls = KreoClass(new_cls_tail)

                self._trie[KreoClass.tail_str(new_cls_tail)] = new_cls

                # Move method to new class in methodToKreoClassMap
                self._method_to_class[method] = set([new_cls])

        # Each method is now associated with exactly one class
        for cls_set in self._method_to_class.values():
            assert len(cls_set) == 1

    def swim_destructors(self):
        # Move destructor functions into correct location in the trie. There is the
        # possibility that a parent object was never constructed but a child was. In
        # this case we know the destructor belongs to the parent but it currently
        # belongs to the child.
        for key, node in self._trie.items():
            # Find tail[-1] in _method_to_class and replace the reference
            # with this class
            destructor = node.tail[-1]
            if destructor in self._method_to_class:
                cls_set = set([node])
                if cls_set != self._method_to_class[destructor]:
                    self._method_to_class[destructor] = cls_set

    def map_trie_nodes_to_methods(self):
        # map trie nodes to methods now that method locations are fixed
        for method, trie_node in self._method_to_class.items():
            if len(trie_node) > 1:
                LOGGER.info("Method mapped to multiple classes %s", method)
            trie_node = list(trie_node)[0]
            self._class_to_method_set[trie_node].add(method)

    def generate_json(self):
        # Output json in OOAnalyzer format

        structures: dict[str, ar.Structure] = {}
        for trie_node in self._trie:
            node, trace = self._trie._get_node(trie_node)
            cls: KreoClass = node.value

            parent_cls: KreoClass | None = None
            if len(trace) > 2:
                parent_cls = trace[-2][1].value

            # For now, while we only detect direct parent relationships, only
            # add a member if we have a parent, and don't actually know anything
            # about its size
            members: dict[str, ar.Member] = {}
            if parent_cls is not None:
                members["0x0"] = ar.Member(
                    name=str(parent_cls) + "_0x0",
                    struc=str(parent_cls),
                )

            methods: dict[str, ar.Method] = {}
            # If there are no methods associated with the trie node there might not
            # be any methods in the set
            if cls in self._class_to_method_set:
                for method in self._class_to_method_set[cls]:
                    method.update_type()
                    method_addr_str = hex(method.address + self._base_offset)
                    methods[method_addr_str] = ar.Method(
                        demangled_name=method.name if method.name else "",
                        ea=method_addr_str,
                        name=method.type + "_" + method_addr_str,
                        type=method.type,
                    )

            structures[str(cls)] = ar.Structure(
                name=str(cls),
                members=members,
                methods=methods,
            )

        final_json = ar.AnalysisResults(
            filename=self.cfg.binary_path.name,
            filemd5=md5File(self.cfg.binary_path),
            structures=structures,
            version="kreo-0.1.0",
        )

        with self.cfg.results_json.open("w") as f:
            f.write(json.dumps(final_json.dict(), indent=4))

    def load_method_candidates(self) -> None:
        for line in self.cfg.method_candidates_path.open():
            self._method_candidate_addresses.add(int(line, 16))

    def main(self):
        ###############################################################################
        # Step: Read traces and other info from disk                                  #
        ###############################################################################
        self.run_step(self.parse_input, "parsing input...", "input parsed")
        LOGGER.info("Found %i traces", len(self._traces))

        ###############################################################################
        # Step: Record how many times each method was seen in the head and            #
        # tail                                                                 #
        ###############################################################################
        self.run_step(
            self.update_all_method_statistics,
            "updating method statistics...",
            "method statistics updated",
        )

        ###############################################################################
        # Step: Split spurious traces                                                 #
        ###############################################################################
        self.run_step(self.split_dynamic_traces, "splitting traces...", "traces split")
        print(f"after splitting there are now {len(self._traces)} traces")

        ###############################################################################
        # Step: Update method statistics again now. Splitting traces won't reveal new #
        # constructors/destructors; however, the number of times methods are seen in  #
        # the head/body/tail does change.                                             #
        ###############################################################################
        self.run_step(
            self.update_all_method_statistics,
            "updating method statistics again...",
            "method statistics updated",
        )

        if (
            self.cfg.eliminate_object_traces_with_matching_initializer_and_finalizer_method
        ):
            pass
            ###############################################################################
            # Step: Remove object-traces where the first trace entry is a call to a       #
            # method and the last trace entry is a return from that same call             #
            # note: This could result in not identifying methods that belong to a class   #
            # that does not have a destructor                                             #
            ###############################################################################
            # self.run_step(
            #     self.eliminate_object_traces_same_init_finalizer,
            #     "eliminating object-traces with identical initializer and finalizer...",
            #     "object-traces updated",
            # )

            ###############################################################################
            # Step: Update method statistics again now. Splitting traces won't reveal new #
            # constructors/destructors; however, the number of times methods are seen in  #
            # the head/body/tail does change.                                             #
            ###############################################################################
            # self.run_step(
            #     self.update_all_method_statistics,
            #     "updating method statistics again...",
            #     "method statistics updated",
            # )

        if self.cfg.heuristic_fingerprint_improvement:
            ###############################################################################
            # Step: Remove from fingerprints any methods that are not identified as       #
            # destructors.                                                                #
            ###############################################################################
            self.run_step(
                self.remove_nondestructors_from_fingerprints,
                "removing methods that aren't destructors from fingerprints...",
                "method removed",
            )

            ###############################################################################
            # Step: Update method statistics again now. Splitting traces won't reveal new #
            # constructors/destructors; however, the number of times methods are seen in  #
            # the head/body/tail does change.                                             #
            ###############################################################################
            self.run_step(
                self.update_all_method_statistics,
                "updating method statistics again...",
                "method statistics updated",
            )

        ###############################################################################
        # Step: Look at fingerprints to determine hierarchy. Insert entries into trie.#
        ###############################################################################
        self.run_step(self.construct_trie, "constructing trie...", "trie constructed")
        self.run_step(self.reorganize_trie, "reorganizing trie...", "trie reorganized")
        self.run_step(
            self.swim_destructors,
            "moving destructors up in trie...",
            "destructors moved up",
        )

        if self.cfg.enable_static_alias_analysis:
            ###############################################################################
            # Step: Load method candidates so we only add candidates from static traces.  #
            ###############################################################################
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
            self.parse_method_names, "parsing method names...", "method names parsed"
        )

        self.run_step(
            self.map_trie_nodes_to_methods,
            "mapping trie nodes to methods...",
            "trie nodes mapped",
        )

        self.run_step(self.generate_json, "generating json...", "json generated")

        TriePrinter(self._class_to_method_set, self._trie)

        LOGGER.debug("Done, Kreo exiting normally.")
