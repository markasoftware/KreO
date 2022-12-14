import json
import uuid
import hashlib
import os
import time
import pygtrie
import subprocess
from copy import copy
from collections import defaultdict
from typing import List, Callable, Dict, Set, Any, Tuple, Optional
from method import Method
from object_trace import ObjectTrace, TraceEntry
from method_store import MethodStore
from static_trace import StaticTrace, StaticTraceEntry
import sys

sys.path.append('../')

from parseconfig import config

def printTraces(traces):
    for trace in traces:
        print(trace)

# =============================================================================
class KreoClass:
    '''
    Represents a value in a trie.
    '''
    def __init__(self, fingerprint: List[Method]):
        self.uuid = str(uuid.uuid4())
        self.fingerprint = fingerprint
        assert len(self.fingerprint) > 0

    def fingerprintStr(fingerprint) -> str:
        fingerprintstr = [str(f) for f in fingerprint]
        return '/'.join(fingerprintstr)

    def __str__(self) -> str:
        # the address associated with this class is the hex address of
        # the last element in the fingerprint, representing the destructor
        # associated with this class
        assert len(self.fingerprint) > 0
        return 'KreoClass-' + self.uuid[0:5] + '@' + hex(self.fingerprint[-1].address)

# =============================================================================
class TriePrinter:
    def __init__(self, kreoClassToMethodSetMap, trie):
        self.indent = ''
        self.kreoClassToMethodSetMap = kreoClassToMethodSetMap
        trie.traverse(self.printTrie)

    def printTrie(self, pathConv, path, children, cls=None):
        if cls is None:
            print(f'{self.indent}n/a')
        else:
            pathC = pathConv(path)
            lastFingerprintMethod = cls.fingerprint[-1]
            print(f'{self.indent}{pathC} {lastFingerprintMethod.isProbablyDestructor()} {lastFingerprintMethod.seenInHead} {lastFingerprintMethod.seenInFingerprint} {lastFingerprintMethod.seenInTorso}')
            if cls in self.kreoClassToMethodSetMap:
                for method in self.kreoClassToMethodSetMap[cls]:
                    method.updateType()
                    print(f'{self.indent}* {method} | {method.seenInHead} {method.seenInFingerprint} {method.seenInTorso}')

        self.indent += '    '
        list(children)
        self.indent = self.indent[0:-4]

# =============================================================================
class Postgame:
    def __init__(self):
        self.traces: Set[ObjectTrace] = set()
        self.staticTraces: Set[StaticTrace] = set()
        self.methodStore = MethodStore()

        self.trie = pygtrie.StringTrie()

        # we need a way to know which trie nodes correspond to each method, so
        # that if a method gets mapped to multiple places we can reassign it to
        # the LCA.
        self.methodToKreoClassMap: Dict[Method, Set[KreoClass]] = defaultdict(set)

        # kreoClassToMethodSetMap does not need to be maintained manually -- it will be populated all at once during the mapTrieNodesToMethods step.
        self.kreoClassToMethodSetMap: DefaultDict[KreoClass, Set[Method]] = defaultdict(set)

    def runStep(self, function: Callable[[None], None], startMsg: str, endMsg: str)-> None:
        '''
        Runs the given step function, eprinting start/end messages and profiling the step's time.
        '''
        print(startMsg)
        startTime = time.perf_counter()
        function()
        endTime = time.perf_counter()
        print(f'{endMsg} ({endTime - startTime:0.2f}s)')

    def parseInput(self):
        self.baseOffset = int(next(iter(open(config['baseOffsetPath']))))

        blacklistedMethods: Set[int] = set()
        for line in open(config['blacklistedMethodsPath']):
            blacklistedMethods.add(int(line))

        def addIfValid(trace: List[TraceEntry]):
            # Sanity check object-trace before adding to traces
            if len(trace) >= 2:
                self.traces.add(ObjectTrace(trace))

        curTrace: List[TraceEntry] = []
        # there can be multiple object trace files...find all of them
        for line in open(config['objectTracesPath']):
            # each line ends with \n, empty line indicates new trace
            if len(line) == 1:
                addIfValid(curTrace)
                curTrace = []
            else:
                addr = int(line.split()[1])
                if addr not in blacklistedMethods:
                    curTrace.append(TraceEntry(line, self.methodStore.findOrInsertMethod))
        # finish the last trace
        addIfValid(curTrace)

    def parseMethodNames(self):
        for line in open(config['objectTracesPath'] + '-name-map'):
            splitlines = line.split()
            try:
                p1 = subprocess.Popen(['demangle', '--noerror', '-n', splitlines[1]], stdout=subprocess.PIPE)
                demangledName = str(p1.stdout.read())
            except FileNotFoundError as _:
                demangledName = splitlines[1]
            self.methodStore.insertMethodName(int(splitlines[0]), demangledName)

    def parseStaticTraces(self):
        curTrace: List[StaticTraceEntry] = []

        def flushCurTrace():
            if len(curTrace) > 0:
                self.staticTraces.add(StaticTrace(curTrace))

        for line in open(config['staticTracesPath']):
            if len(line) == 1:
                flushCurTrace()
            elif line[0] != '#':
                curTrace.append(StaticTraceEntry(line, self.methodStore.findOrInsertMethod))

        flushCurTrace()

    def updateAllMethodStatistics(self):
        self.methodStore.resetAllMethodStatistics()
        for trace in self.traces:
            trace.updateMethodStatistics()

    def splitTracesFn(self):
        self.traces = set([splittedTrace for trace in self.traces for splittedTrace in trace.split()])

    def splitStaticTraces(self):
        self.staticTraces = set([splittedTrace for trace in self.staticTraces for splittedTrace in trace.split()])

    def discoverMethodsStatically(self):
        '''
        Uses the data from the static traces to discover new methods and assign them to the appropriate class. Does not update how dynamically-detected methods are assigned, because their destructor fingerprints usually provide more information about hierarchy than we can hope to glean from such a simple static analysis (we just assign things to the LCA of all the classes of all dynamically detected methods in the traces).
        '''
        # In reality, this routine is only called after the first reorganization, so every dynamically detected method is already assigned to a single class, not a set, but we write assuming it is assigned to a set anyway to be safe (e.g., no next(iter(...)))
        for staticTrace in self.staticTraces:
            traceEntries = set(staticTrace.entries)
            dynamicEntries = set(filter(lambda entry: self.dynamicallyDetectedMethods.getMethod(entry.method.address), traceEntries))
            staticEntries = traceEntries - dynamicEntries
            for staticEntry in staticEntries:
                staticMethod = staticEntry.method
                for dynamicEntry in dynamicEntries:
                    dynamicMethod = dynamicEntry.method
                    self.methodToKreoClassMap[staticMethod].update(self.methodToKreoClassMap[dynamicMethod])

    def removeNondestructorsFromFingerprints(self):
        for trace in self.traces:
            trace.fingerprint = list(filter(lambda method: method.isProbablyDestructor(), trace.fingerprint))
        # Remove traces with empty fingerprints
        self.traces = set(filter(lambda trace: len(trace.fingerprint) > 0, self.traces))

    def constructTrie(self):
        for trace in self.traces:
            # Insert class and any parents into the trie
            for i in range(len(trace.fingerprint)):
                partialFingerprint = trace.fingerprint[0:i + 1]
                if KreoClass.fingerprintStr(partialFingerprint) not in self.trie.keys():
                    newCls = KreoClass(partialFingerprint)
                    self.trie[KreoClass.fingerprintStr(partialFingerprint)] = newCls

            cls = self.trie[KreoClass.fingerprintStr(trace.fingerprint)]

            for method in trace.methods():
                self.methodToKreoClassMap[method].add(cls)

    def trieLCA(self, classes: Set[KreoClass]):
        def findLongestSharedTrace(traces):
            minLenTrace = None
            for trace in traces:
                if minLenTrace is None or len(minLenTrace[1]) > len(trace[1]):
                    minLenTrace = trace
                    
            for i in range(len(minLenTrace)):
                for trace in traces:
                    if trace[1][i] != traces[0][1][i]:
                        return trace[1][:i]
                
            return minLenTrace[1]

        clsTraces = list()
        for cls in classes:
            clsTraces.append(self.trie._get_node(KreoClass.fingerprintStr(cls.fingerprint)))

        sharedTrace = findLongestSharedTrace(clsTraces)

        if len(sharedTrace) > 1:
            return sharedTrace[-1][1].value
        else:
            return None

    def reorganizeTrie(self):
        for method, clsSet in self.methodToKreoClassMap.items():
            lca = self.trieLCA(clsSet)

            if lca:
                # LCA exists
                self.methodToKreoClassMap[method] = set([lca])
            else:
                # LCA doesn't exist, have to add class to trie
                
                # Create class that will be inserted into the trie. This
                # class must have a new fingerprint that is the method
                # that will be assigned to the class that the two
                # classes share.
                newClassFingerprint = [method]
                newClass = KreoClass(newClassFingerprint)

                self.trie[KreoClass.fingerprintStr(newClassFingerprint)] = newClass

                # Move method to new class in methodToKreoClassMap
                self.methodToKreoClassMap[method] = set([newClass])

                # Remove from trie all classes that are being moved and update each class's fingerprint, then reinsert into trie
                for cls in clsSet:
                    self.trie.pop(KreoClass.fingerprintStr(cls.fingerprint))
                    cls.fingerprint = newClass.fingerprint + cls.fingerprint
                    self.trie[KreoClass.fingerprintStr(cls.fingerprint)] = cls

        # Each method is now associated with exactly one class
        for clsSet in self.methodToKreoClassMap.values():
            assert(len(clsSet) == 1)

    def swimDestructors(self):
        # Move destructor functions into correct location in the trie. There is the
        # possibility that a parent object was never constructed but a child was. In
        # this case we know the destructor belongs to the parent but it currently
        # belongs to the child.
        for key in self.trie:
            # Find fingerprint[-1] in methodToKreoClassMap and replace the reference
            # with this class
            node = self.trie[key]
            if node.fingerprint[-1] in self.methodToKreoClassMap:
                self.methodToKreoClassMap[node.fingerprint[-1]] = set([node])

    def recordDynamicallyDetectedMethods(self):
        self.dynamicallyDetectedMethods = copy(self.methodStore)

    def mapTrieNodesToMethods(self):
        # map trie nodes to methods now that method locations are fixed
        for method, trieNode in self.methodToKreoClassMap.items():
            trieNode = list(trieNode)[0]
            self.kreoClassToMethodSetMap[trieNode].add(method)

    def generateJson(self):
        # Output json in OOAnalyzer format

        # copied from https://stackoverflow.com/a/3431838/1233320
        def md5File(fname):
            hashMd5 = hashlib.md5()
            with open(fname, "rb") as f:
                for chunk in iter(lambda: f.read(4096), b""):
                    hashMd5.update(chunk)
            return hashMd5.hexdigest()

        structures: Dict[str, Dict[str, Any]] = dict()
        for trieNode in self.trie:
            node, trace = self.trie._get_node(trieNode)
            cls: KreoClass = node.value

            parentCls: KreoClass = None
            if len(trace) > 2:
                parentCls = trace[-2][1].value

            # For now, while we only detect direct parent relationships, only
            # add a member if we have a parent, and don't actually know anything
            # about its size
            members = {}
            if parentCls is not None:
                members['0x0'] = {
                    'base': False, # TODO: what does this one even mean?
                    'name': str(parentCls) + '_0x0',
                    'offset': '0x0',
                    'parent': True,
                    'size': 4,
                    'struc': str(parentCls),
                    'type': 'struc',
                    'usages': [],
                }

            methods = dict()
            # If there are no methods associated with the trie node there might not be any methods in the set
            if cls in self.kreoClassToMethodSetMap:
                for method in self.kreoClassToMethodSetMap[cls]:
                    method.updateType()
                    methodAddrStr = hex(method.address + self.baseOffset)
                    methods[methodAddrStr] = {
                        'demangled_name': method.name if method.name else '',
                        'ea': methodAddrStr,
                        'import': False,
                        'name': method.type + "_" + methodAddrStr,
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

        jsonFile = open(config['resultsPath'], 'w')
        jsonFile.write(json.JSONEncoder(indent = None if config['resultsIndent'] == 0 else config['resultsIndent']).encode(finalJson))

    def main(self):
        ###############################################################################
        # Step: Read traces and other info from disk                                  #
        ###############################################################################
        self.runStep(self.parseInput, 'parsing input...', f'input parsed')
        print(f'found {len(self.traces)} traces')

        ###############################################################################
        # Step: Record how many times each method was seen in the head and            #
        # fingerprint                                                                 #
        ###############################################################################
        self.runStep(self.updateAllMethodStatistics, 'updating method statistics...', 'method statistics updated')

        ###############################################################################
        # Step: Split spurious traces                                                 #
        ###############################################################################
        self.runStep(self.splitTracesFn, 'splitting traces...', f'traces split')
        print(f'after splitting there are now {len(self.traces)} traces')

        ###############################################################################
        # Step: Update method statistics again now. Splitting traces won't reveal new #
        # constructors/destructors; however, the number of times methods are seen in  #
        # the head/body/fingerprint does change.                                      #
        ###############################################################################
        self.runStep(self.updateAllMethodStatistics, 'updating method statistics again...', 'method statistics updated')

        ###############################################################################
        # Step: Remove from fingerprints any methods that are not identified as       #
        # destructors.                                                                #
        ###############################################################################
        self.runStep(self.removeNondestructorsFromFingerprints, 'removing methods that aren\'t destructors from fingerprints...', 'method removed')

        ###############################################################################
        # Step: Update method statistics again now. Splitting traces won't reveal new #
        # constructors/destructors; however, the number of times methods are seen in  #
        # the head/body/fingerprint does change.                                      #
        ###############################################################################
        self.runStep(self.updateAllMethodStatistics, 'updating method statistics again...', 'method statistics updated')
        self.runStep(self.recordDynamicallyDetectedMethods, 'recording dynamic methods...', 'dynamic methods recoded')

        ###############################################################################
        # Step: Look at fingerprints to determine hierarchy. Insert entries into trie.#
        ###############################################################################
        self.runStep(self.constructTrie, 'constructing trie...', 'trie constructed')
        self.runStep(self.reorganizeTrie, 'reorganizing trie...', 'trie reorganized')
        self.runStep(self.swimDestructors, 'moving destructors up in trie...', 'destructors moved up')

        if config['enableAliasAnalysis']:
            self.runStep(self.parseStaticTraces, 'parsing static traces...', 'static traces parsed')
            self.runStep(self.splitStaticTraces, 'splitting static traces...', 'static traces split')
            self.runStep(self.discoverMethodsStatically, 'discovering methods from static traces...', 'static methods discovered')
            # discoverMethodsStatically leaves the new methods assigned to sets of classes still
            self.runStep(self.reorganizeTrie, '2nd reorganizing trie...', '2nd trie reorganization complete')

        self.runStep(self.parseMethodNames, 'parsing method names...', 'method names parsed')

        self.runStep(self.mapTrieNodesToMethods, 'mapping trie nodes to methods...', 'trie nodes mapped')
        self.runStep(self.generateJson, 'generating json...', 'json generated')

        TriePrinter(self.kreoClassToMethodSetMap, self.trie)

        print('Done, Kreo exiting normally.')

if __name__ == '__main__':
    Postgame().main()
