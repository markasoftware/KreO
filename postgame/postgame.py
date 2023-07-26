import json
import uuid
import hashlib
import os
import time
import pygtrie
import subprocess
import sys
import pathlib
import parse_object_trace

from collections import defaultdict
from typing import List, Callable, Dict, Set, Any, Tuple
from method import Method
from object_trace import ObjectTrace, TraceEntry
from method_store import MethodStore
from static_trace import StaticTrace, StaticTraceEntry

fpath = pathlib.Path(__file__).parent.absolute()

sys.path.append(os.path.join(fpath, '..'))

from parseconfig import parseconfig_argparse

config = parseconfig_argparse()

def printTraces(traces):
    for trace in traces:
        print(trace)

# =============================================================================
class KreoClass:
    '''
    Represents a value in a trie.
    '''
    def __init__(self, tail: List[Method]):
        self.uuid = str(uuid.uuid4())
        self.tail = tail
        assert len(self.tail) > 0

    def tailStr(tail) -> str:
        fstr = [str(f) for f in tail]
        return '/'.join(fstr)

    def __str__(self) -> str:
        # the address associated with this class is the hex address of
        # the last element in the tail, representing the destructor
        # associated with this class
        assert len(self.tail) > 0
        return 'KreoClass-' + self.uuid[0:5] + '@' + hex(self.tail[-1].address)

# =============================================================================
class TriePrinter:
    def __init__(self, kreo_class_to_method_set_map, trie):
        self._indent = ''
        self._kreo_class_to_method_set_map = kreo_class_to_method_set_map
        trie.traverse(self.printTrie)

    def printTrie(self, pathConv, path, children, cls=None):
        if cls is None:
            print(f'{self._indent}n/a')
        else:
            pathC = pathConv(path)
            lastFingerprintMethod = cls.tail[-1]
            print(f'{self._indent}{pathC} {lastFingerprintMethod.isProbablyDestructor()} {lastFingerprintMethod.seen_in_head} {lastFingerprintMethod.seen_in_tail} {lastFingerprintMethod.seen_in_torso}')
            if cls in self._kreo_class_to_method_set_map:
                for method in self._kreo_class_to_method_set_map[cls]:
                    method.updateType()
                    print(f' {self._indent}* {method} | {method.seen_in_head} {method.seen_in_tail} {method.seen_in_torso}')

        self._indent += '    '
        list(children)
        self._indent = self._indent[0:-4]

# =============================================================================
class Postgame:
    def __init__(self):
        self._traces: Set[ObjectTrace] = set()
        self._static_traces: Set[StaticTrace] = set()
        self._method_store = MethodStore()

        self._trie = pygtrie.StringTrie()

        # we need a way to know which trie nodes correspond to each method, so
        # that if a method gets mapped to multiple places we can reassign it to
        # the LCA.
        self._method_to_kreo_class_map: Dict[Method, Set[KreoClass]] = defaultdict(set)

        # kreo_class_to_method_set_map does not need to be maintained manually -- it will be populated all at once during the mapTrieNodesToMethods step.
        self._kreo_class_to_method_set_map: Dict[KreoClass, Set[Method]] = defaultdict(set)

        self._method_candidates: Set[int] = set()

    def runStep(self, function: Callable[[None], None], start_msg: str, end_msg: str)-> None:
        '''
        Runs the given step function, eprinting start/end messages and profiling the step's time.
        '''
        print(start_msg)
        start_time = time.perf_counter()
        function()
        end_time = time.perf_counter()
        print(f'{end_msg} ({end_time - start_time:0.2f}s)')

    def parseInput(self):
        self._base_offset, self._traces = parse_object_trace.parseInput(config, self._method_store)

    def parseMethodNames(self):
        for line in open(config['objectTracesPath'] + '-name-map'):
            method_addr = int(line[:line.find(' ')], 16)
            mangled_name = line[line.find(' ') + 1:-1]
            try:
                p1 = subprocess.Popen(['undname', mangled_name], stdout=subprocess.PIPE)
                demangled_name = str(p1.stdout.read())
                demangled_name = demangled_name.split('\\n')
                demangled_name = demangled_name[4]
                demangled_name = demangled_name[demangled_name.find('"') + 1:demangled_name.rfind('"')]
            except FileNotFoundError as _:
                demangled_name = mangled_name
            self._method_store.insertMethodName(method_addr, demangled_name)

    def parseStaticTraces(self):
        cur_trace: List[StaticTraceEntry] = []

        def flushCurTrace():
            if len(cur_trace) > 0:
                self._static_traces.add(StaticTrace(cur_trace))

        for line in open(config['staticTracesPath']):
            if line == "\n":
                flushCurTrace()
            elif line[0] != '#':
                addr = int(line.split()[0], 16)
                if addr in self._method_candidates:
                    cur_trace.append(StaticTraceEntry(line, self._method_store.findOrInsertMethod))

        flushCurTrace()

    def updateAllMethodStatistics(self):
        self._method_store.resetAllMethodStatistics()
        for trace in self._traces:
            trace.updateMethodStatistics()

    def splitTracesFn(self):
        self._traces = set([splitted_trace for trace in self._traces for splitted_trace in trace.split()])

    def splitStaticTraces(self):
        self._static_traces = set([splitted_trace for trace in self._static_traces for splitted_trace in trace.split()])

    def eliminateObjectTracesSameInitFinalizer(self):
        new_traces: Set[ObjectTrace] = set()
        for trace in self._traces:
            # TODO
            # if trace.head[0] is not trace.tail[-1]:
            new_traces.add(trace)
        self._traces = new_traces

    def discoverMethodsStatically(self):
        '''
        Uses the data from the static traces to discover new methods and assign
        them to the appropriate class. Does not update how dynamically-detected
        methods are assigned, because their destructor fingerprints usually
        provide more information about hierarchy than we can hope to glean from
        such a simple static analysis (we just assign things to the LCA of all
        the classes of all dynamically detected methods in the traces).
        '''
        # In reality, this routine is only called after the first
        # reorganization, so every dynamically detected method is already
        # assigned to a single class, not a set, but we write assuming it is
        # assigned to a set anyway to be safe (e.g., no next(iter(...)))
        for static_trace in self._static_traces:
            trace_entries = set(static_trace.entries)

            dynamic_entries: Set[StaticTraceEntry] = set()
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

                    if dynamic_method in self._method_to_kreo_class_map:
                        self._method_to_kreo_class_map[static_method].update(self._method_to_kreo_class_map[dynamic_method])

    def removeNondestructorsFromFingerprints(self):
        for trace in self._traces:
            trace.tail = list(filter(lambda method: method.isProbablyDestructor(), trace.tail))
        # Remove traces with empty fingerprints
        self._traces = set(filter(lambda trace: len(trace.tail) > 0, self._traces))

    def constructTrie(self):
        for trace in self._traces:
            # Insert class and any parents into the trie using the trace's tail
            for i in range(len(trace.tail)):
                partial_tail = trace.tail[0:i + 1]
                partial_tail_str = KreoClass.tailStr(partial_tail)
                if partial_tail_str not in self._trie.keys():
                    self._trie[partial_tail_str] = KreoClass(partial_tail)

            # Map all methods in the trace to the class in the trie
            cls = self._trie[KreoClass.tailStr(trace.tail)]
            for method in trace.methods():
                self._method_to_kreo_class_map[method].add(cls)

    def trieLCA(self, classes: Set[KreoClass]) -> KreoClass:
        '''
        Finds the least common ancestor between the set of classes. The LCA is a KreoClass.
        '''
        def findLCA(cls_trie_node_lists: List[List[Tuple[str, pygtrie._Node]]]) -> List[Tuple[str, pygtrie._Node]] :
            shortest_node_list = None
            for node_list in cls_trie_node_lists:
                if shortest_node_list is None or len(shortest_node_list) > len(node_list):
                    shortest_node_list = node_list
            assert shortest_node_list != None

            # Check to see if other classes have identical starting nodes.
            for i in range(len(shortest_node_list)):
                for node_list in cls_trie_node_lists:
                    if node_list[i][0] != shortest_node_list[i][0]:
                        return node_list[:i]

            return shortest_node_list

        cls_trie_node_lists: List[List[Tuple[str, pygtrie._Node]]] = [self._trie._get_node(KreoClass.tailStr(cls.tail))[1] for cls in classes]

        shared_node_list = findLCA(cls_trie_node_lists)

        if len(shared_node_list) > 1:
            # Shared class is KreoClass associated with the final node.
            return shared_node_list[-1][1].value
        else:
            return None

    def reorganizeTrie(self):
        # ensure each method is associated with exactly one class by swimming
        # methods to higher trie nodes.
        for method, cls_set in self._method_to_kreo_class_map.items():
            lca = self.trieLCA(cls_set)

            if lca:
                # LCA exists
                self._method_to_kreo_class_map[method] = set([lca])
            else:
                # LCA doesn't exist, have to add class to trie

                # Create class that will be inserted into the trie. This
                # class must have a new tail that is the method
                # that will be assigned to the class that the two
                # classes share.
                new_cls_fingerprint = [method]
                new_cls = KreoClass(new_cls_fingerprint)

                self._trie[KreoClass.tailStr(new_cls_fingerprint)] = new_cls

                # Move method to new class in methodToKreoClassMap
                self._method_to_kreo_class_map[method] = set([new_cls])

                # Remove from trie all classes that are being moved and update each class's tail, then reinsert into trie
                for cls in cls_set:
                    self._trie.pop(KreoClass.tailStr(cls.tail))
                    cls.tail = new_cls.tail + cls.tail
                    self._trie[KreoClass.tailStr(cls.tail)] = cls

        # Each method is now associated with exactly one class
        for cls_set in self._method_to_kreo_class_map.values():
            assert(len(cls_set) == 1)

    def swimDestructors(self):
        # Move destructor functions into correct location in the trie. There is the
        # possibility that a parent object was never constructed but a child was. In
        # this case we know the destructor belongs to the parent but it currently
        # belongs to the child.
        for key in self._trie:
            # Find tail[-1] in methodToKreoClassMap and replace the reference
            # with this class
            node = self._trie[key]
            destructor = node.tail[-1]
            if destructor in self._method_to_kreo_class_map:
                cls_set = set([node])
                if cls_set != self._method_to_kreo_class_map[destructor]:
                    self._method_to_kreo_class_map[destructor] = cls_set

    def mapTrieNodesToMethods(self):
        # map trie nodes to methods now that method locations are fixed
        for method, trie_node in self._method_to_kreo_class_map.items():
            if len(trie_node) > 1:
                print(f'Method mapped to multiple classes {method}')
            trie_node = list(trie_node)[0]
            self._kreo_class_to_method_set_map[trie_node].add(method)

    def generateJson(self):
        # Output json in OOAnalyzer format

        # copied from https://stackoverflow.com/a/3431838/1233320
        def md5File(fname):
            hash_md5 = hashlib.md5()
            try:
                with open(fname, "rb") as f:
                    for chunk in iter(lambda: f.read(4096), b""):
                        hash_md5.update(chunk)
                return hash_md5.hexdigest()
            except FileNotFoundError:
                print('could not find exe to generate md5')
                return 'na'

        structures: Dict[str, Dict[str, Any]] = dict()
        for trie_node in self._trie:
            node, trace = self._trie._get_node(trie_node)
            cls: KreoClass = node.value

            parent_cls: KreoClass = None
            if len(trace) > 2:
                parent_cls = trace[-2][1].value

            # For now, while we only detect direct parent relationships, only
            # add a member if we have a parent, and don't actually know anything
            # about its size
            members = {}
            if parent_cls is not None:
                members['0x0'] = {
                    'base': False, # TODO: what does this one even mean?
                    'name': str(parent_cls) + '_0x0',
                    'offset': '0x0',
                    'parent': True,
                    'size': 4,
                    'struc': str(parent_cls),
                    'type': 'struc',
                    'usages': [],
                }

            methods = dict()
            # If there are no methods associated with the trie node there might not be any methods in the set
            if cls in self._kreo_class_to_method_set_map:
                for method in self._kreo_class_to_method_set_map[cls]:
                    method.updateType()
                    method_addr_str = hex(method.address + self._base_offset)
                    methods[method_addr_str] = {
                        'demangled_name': method.name if method.name else '',
                        'ea': method_addr_str,
                        'import': False,
                        'name': method.type + "_" + method_addr_str,
                        'type': method.type,
                    }

            structures[str(cls)] = {
                'demangled_name': '', # TODO: use RTTI to get this if possible?
                'name': str(cls),
                'members': members,
                'methods': methods,
                'size': 0, # TODO: this
                'vftables': [],
            }

        finalJson = {
            'filename': os.path.basename(config['binaryPath']),
            'filemd5': md5File(config['binaryPath']),
            'structures': structures,
            'vcalls': dict(), # not sure what this is
            'version': 'kreo-0.1.0',
        }

        json_file = open(config['resultsJson'], 'w')
        json_file.write(json.JSONEncoder(indent = None if config['resultsIndent'] == 0 else config['resultsIndent']).encode(finalJson))

    def loadMethodCandidates(self):
        with open(config['methodCandidatesPath'], 'r') as f:
            for line in f:
                self._method_candidates.add(int(line, 16))

    def main(self):
        ###############################################################################
        # Step: Read traces and other info from disk                                  #
        ###############################################################################
        self.runStep(self.parseInput, 'parsing input...', f'input parsed')
        print(f'found {len(self._traces)} traces')

        ###############################################################################
        # Step: Record how many times each method was seen in the head and            #
        # tail                                                                 #
        ###############################################################################
        self.runStep(self.updateAllMethodStatistics, 'updating method statistics...', 'method statistics updated')

        ###############################################################################
        # Step: Split spurious traces                                                 #
        ###############################################################################
        self.runStep(self.splitTracesFn, 'splitting traces...', f'traces split')
        print(f'after splitting there are now {len(self._traces)} traces')

        ###############################################################################
        # Step: Update method statistics again now. Splitting traces won't reveal new #
        # constructors/destructors; however, the number of times methods are seen in  #
        # the head/body/tail does change.                                             #
        ###############################################################################
        self.runStep(self.updateAllMethodStatistics, 'updating method statistics again...', 'method statistics updated')

        # if config['eliminateObjectTracesWithMatchingInitializerAndFinalizerMethod']:
        #     ###############################################################################
        #     # Step: Remove object-traces where the first trace entry is a call to a       #
        #     # method and the last trace entry is a return from that same call             #
        #     # note: This could result in not identifying methods that belong to a class   #
        #     # that does not have a destructor                                             #
        #     ###############################################################################
        #     self.runStep(self.eliminateObjectTracesSameInitFinalizer, 'eliminating object-traces with identical initializer and finalizer...', 'object-traces updated')

        #     ###############################################################################
        #     # Step: Update method statistics again now. Splitting traces won't reveal new #
        #     # constructors/destructors; however, the number of times methods are seen in  #
        #     # the head/body/tail does change.                                             #
        #     ###############################################################################
        #     self.runStep(self.updateAllMethodStatistics, 'updating method statistics again...', 'method statistics updated')

        if config['heuristicFingerprintImprovement']:
            ###############################################################################
            # Step: Remove from fingerprints any methods that are not identified as       #
            # destructors.                                                                #
            ###############################################################################
            self.runStep(self.removeNondestructorsFromFingerprints, 'removing methods that aren\'t destructors from fingerprints...', 'method removed')

            ###############################################################################
            # Step: Update method statistics again now. Splitting traces won't reveal new #
            # constructors/destructors; however, the number of times methods are seen in  #
            # the head/body/tail does change.                                             #
            ###############################################################################
            self.runStep(self.updateAllMethodStatistics, 'updating method statistics again...', 'method statistics updated')

        ###############################################################################
        # Step: Look at fingerprints to determine hierarchy. Insert entries into trie.#
        ###############################################################################
        self.runStep(self.constructTrie, 'constructing trie...', 'trie constructed')
        self.runStep(self.reorganizeTrie, 'reorganizing trie...', 'trie reorganized')
        self.runStep(self.swimDestructors, 'moving destructors up in trie...', 'destructors moved up')

        if config['enableAliasAnalysis']:
            ###############################################################################
            # Step: Load method candidates so we only add candidates from static traces.  #
            ###############################################################################
            self.runStep(self.loadMethodCandidates, 'loading method candidates...', 'method candidates loaded')

            self.runStep(self.parseStaticTraces, 'parsing static traces...', 'static traces parsed')
            self.runStep(self.splitStaticTraces, 'splitting static traces...', 'static traces split')
            self.runStep(self.discoverMethodsStatically, 'discovering methods from static traces...', 'static methods discovered')
            # discoverMethodsStatically leaves the new methods assigned to sets of classes still

            # Don't reorganize the trie based on statically discovered methods
            # due to low precision of the method being assigned to the correct class.
            # self.runStep(self.reorganizeTrie, '2nd reorganizing trie...', '2nd trie reorganization complete')

        self.runStep(self.parseMethodNames, 'parsing method names...', 'method names parsed')

        self.runStep(self.mapTrieNodesToMethods, 'mapping trie nodes to methods...', 'trie nodes mapped')

        self.runStep(self.generateJson, 'generating json...', 'json generated')

        TriePrinter(self._kreo_class_to_method_set_map, self._trie)

        print('Done, Kreo exiting normally.')

if __name__ == '__main__':
    Postgame().main()
