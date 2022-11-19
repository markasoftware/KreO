import json
import itertools
import uuid
import hashlib
import os
import time
import pygtrie
import subprocess
from parseconfig import config
from typing import List, Callable, Dict, Set, Any

baseAddr = 0x400000

class Method:
    def __init__(self, address):
        self.address = address
        self.type = ''
        # How many times the method has been seen in different parts of a trace:
        self.seenInHead = int(0)
        self.seenInFingerprint = int(0)
        self.seenTotal = int(0)

        self.destructorTailToTorsoRatioMax = 4
        self.constructorHeadToTorsoRatioMax = 4

    def seenInTorso(self):
        return self.seenTotal - self.seenInHead - self.seenInFingerprint

    def isInFingerprint(self) -> bool:
        return self.seenInFingerprint > 0

    def isInHead(self) -> bool:
        return self.seenInHead > 0

    def isConstructor(self) -> bool:
        '''
        Returns true if method believed to be a destructor. While a method is
        likely a constructor if it appears in the head, we make the assumption
        that it cannot be a constructor if it appears in the fingerprint. Also,
        if the method is not found mostly in the head but instead is found
        somewhat regularly in the body of traces, it is likely not a constructor
        either.
        '''
        return self.isInHead() and \
               not self.isInFingerprint() and \
               self.constructorHeadToTorsoRatioMax * self.seenInTorso() <= self.seenInHead

    def isDestructor(self) -> bool:
        '''
        @see isConstructor. The same idea here but for destructors.
        '''
        return self.isInFingerprint() and \
               not self.isInHead() and \
               self.destructorTailToTorsoRatioMax * self.seenInTorso() <= self.seenInFingerprint

    def updateType(self) -> None:
        # TODO other types may be viable options (virtual methods for example), but for now we don't care about them
        assert not (self.isConstructor() and self.isDestructor())

        if self.isInFingerprint():
            self.type = 'dtor'
        elif self.isInHead():
            self.type = 'ctor'
        else:
            self.type = 'meth'

    def __str__(self) -> str:
        name = findMethodName(self)
        return ('' if name == None else (name + ' ')) +\
               (hex(self.address + baseAddr)) +\
               ('' if self.type == '' else ' ' + self.type)

class TraceEntry:
    def __init__(self, line: str):
        '''
        Contruct a trace entry from the given trace entry
        '''
        splitLine = line.split()
        if len(splitLine) == 2:
            self.method = findOrInsertMethod(int(splitLine[0]))
            self.isCall = int(splitLine[1]) == 1
        else:
            raise Exception('Could not parse trace entry from line: "' + line + '"')

    def __str__(self) -> str:
        return str(self.method) + ' ' + ('1' if self.isCall else '0')

    def __eq__(self, other) -> bool:
        return self.method is other.method and self.isCall == other.isCall

    def __hash__(self):
        return hash(self.__str__())

class Trace:
    def __init__(self, traceEntries: List[TraceEntry]):
        self.traceEntries = traceEntries
        # Fingerprint is in reverse order -- last call in the trace first
        self.head = list(map(lambda entry: entry.method, itertools.takewhile(lambda entry: entry.isCall, traceEntries)))
        self.fingerprint = list(map(lambda entry: entry.method, itertools.takewhile(lambda entry: not entry.isCall, reversed(traceEntries))))
        self.fingerprint.reverse()

    def __str__(self) -> str:
        return '\n'.join(map(lambda te: te.__str__(), self.traceEntries))

    def __hash__(self):
        return hash(self.__str__())

    def __eq__(self, other) -> bool:
        return self.__str__() == other.__str__()

    def updateMethodStatistics(self):
        '''
        Update call statistics for all methods associated with the trace.
        Store number of appearances for each method in each part of the trace.
        '''

        # Count the total number of times each method in the trace is seen
        # anywhere. Note that we will be modifying the global method's "seenCount".
        for entry in self.traceEntries:
            # Only count returns to avoid double counting the number of methods seen
            if not entry.isCall:
                entry.method.seenTotal += 1
                
        for headMethod in self.head:
            headMethod.seenInHead += 1
        # Count the number of methods seen in the fingerprint
        for fingerprintMethod in self.fingerprint:
            fingerprintMethod.seenInFingerprint += 1

    def methods(self):
        '''
        Return a list of methods in the trace.
        '''
        return map(lambda entry: entry.method, filter(lambda entry: entry.isCall, self.traceEntries))

    def split(self):
        '''
        Given a set of destructors, return a list of traces created from this
        one. Returns just itself if no splitting is necessary.
        '''
        splitTraces: List[List[TraceEntry]] = []
        currTrace: List[TraceEntry] = []

        entriesIter = iter(self.traceEntries)
        currEntry: TraceEntry = None

        def iterateAndInsert() -> TraceEntry:
            ce = next(entriesIter, None)
            if ce is not None:
                currTrace.append(ce)
            return ce

        currEntry = iterateAndInsert()
        while currEntry is not None:
            # If entry is a destructor and we are returning from it, potentially split trace
            if currEntry.method.isInFingerprint() and not currEntry.isCall:
                # Iterate until curr entry not destructor
                while currEntry is not None and currEntry.method.isInFingerprint():
                    currEntry = iterateAndInsert()

                # If curr entry is a constructor, split the trace
                if currEntry is not None and currEntry.method.isInHead():
                    # NOTE: must move currEntry from the current trace to the new currTrace
                    splitTraces.append(currTrace[0:-1])
                    currTrace = [currEntry]

                # Otherwise don't split the trace
            currEntry = iterateAndInsert()

        if currTrace != []:
            splitTraces.append(currTrace)

        # Validate the traces generated are valid (all traces must have at least
        # two entries) and none of the entries in any trace should be None
        for trace in splitTraces:
            assert (len(trace) >= 2)
            for entry in trace:
                assert(entry is not None)

        return map(Trace, splitTraces)

methods: Dict[int, Method] = dict()  # map from address to method
methodNames: Dict[Method, str] = dict()  # map from method to method name

def findOrInsertMethod(address: int) -> Method:
    '''
    Attempts to find the method in the global methods map. If the function fails
    to find a method, one will be inserted.
    '''
    global methods

    if address not in methods:
        methods[address] = Method(address)
    return methods[address]

def insertMethodName(methodAddress: int, name: str) -> None:
    '''
    Inserts the method in the methodNames map. Method name will only be inserted
    if there exists a method in the methods map with the methodAddress given.
    Therefore, method names should be inserted after the methods map is finalized.
    '''
    global methodNames
    global methods
    if methodAddress in methods:
        methodNames[methods[methodAddress]] = name

def findMethodName(method: Method) -> str:
    '''
    Finds and return the method name for the given method. Returns None if method name not found.
    '''
    global methodNames
    return methodNames.get(method, None)

def printTraces(traces):
    for trace in traces:
        print(trace)

def runStep(function: Callable[[None], None], startMsg: str, endMsg: str)-> None:
    '''
    Runs the given step function, printing start/end messages and profiling the step's time
    '''
    print(startMsg)
    startTime = time.perf_counter()
    function()
    endTime = time.perf_counter()
    print(f'{endMsg} ({endTime - startTime:0.2f}s)')

###############################################################################
# Step Read traces from disk                                                  #
###############################################################################

# Set of ground truth methods (for testing purposes)
gtMethods: Set[int] = set()

def parseTraces():
    global traces

    curTrace: List[TraceEntry] = []
    # there can be multiple object trace files...find all of them

    for line in open(config['objectTracesPath']):
        # each line ends with \n, empty line indicates new trace
        if len(line) == 1:
            if curTrace:
                traces.append(Trace(curTrace))
                curTrace = []
        else:
            # TODO this is a hack to avoid the delete operator fix this in Game.cpp
            if '233472' not in line:
                curTrace.append(TraceEntry(line))
    # finish the last trace
    if curTrace:
        traces.append(Trace(curTrace))

    for line in open(config['objectTracesPath'] + '-name-map'):
        splitlines = line.split()
        p1 = subprocess.Popen(['demangle', '--noerror', '-n', splitlines[1]], stdout=subprocess.PIPE)
        demangled_name = str(p1.stdout.read())
        insertMethodName(int(splitlines[0]), demangled_name)
    
    for line in open(config['gtMethodsPath']):
        gtMethods.add(int(line))

traces = []
runStep(parseTraces, 'parsing traces...', f'traces parsed')
print(f'found {len(traces)} traces')

###############################################################################
# Step: Record how many times each method was seen in the head and            #
# fingerprint                                                                 #
###############################################################################

def updateAllMethodStatistics():
    global traces
    global methods
    for method in methods.values():
        method.seenInHead = 0
        method.seenInFingerprint = 0
        method.seenTotal = 0
    for trace in traces:
        trace.updateMethodStatistics()

runStep(updateAllMethodStatistics,
    'updating method statistics...',
    'method statistics updated')

###############################################################################
# Step Split spurious traces                                                  #
###############################################################################

def splitTracesFn():
    global traces
    splitTraces: List[Trace] = []
    for trace in traces:
        splitTraces += trace.split()
    traces = splitTraces
runStep(splitTracesFn, 'splitting traces...', f'traces split')
print(f'after splitting there are now {len(traces)} traces')

###############################################################################
# Step: Remove Duplicate traces after splitting                               #
###############################################################################

def removeDuplicateTraces():
    global traces
    tracesSet: Set[Trace] = set()
    for trace in traces:
        tracesSet.add(trace)
    traces = list(tracesSet)

    with open('out/object-traces-no-duplicates', 'w') as f:
        for trace in traces:
            for entry in trace.traceEntries:
                f.write(str(entry) + '\n')

            f.write('\n')
runStep(removeDuplicateTraces, 'removing duplicates...', f'duplicates removed')
print(f'now are {len(traces)} unique traces')

###############################################################################
# Step: Update method statistics again now. Splitting traces won't reveal new #
# constructors/destructors; however, the number of times methods are seen in  #
# the head/body/fingerprint does change.                                      #
###############################################################################

runStep(updateAllMethodStatistics,
    'updating method statistics again...',
    'method statistics updated')

###############################################################################
# Step: Remove from fingerprints any methods that are not identified as       #
# destructors.                                                                #
###############################################################################

def removeNondestructorsFromFingerprints():
    global methods
    global traces
    for trace in traces:
        trace.fingerprint = [method for method in trace.fingerprint if method.isDestructor()]
    # Remove traces with empty fingerprints
    traces = list(filter(lambda trace: len(trace.fingerprint) > 0, traces))
runStep(removeNondestructorsFromFingerprints,
    'removing methods that aren\'t destructors from fingerprints...',
    'method removed')

###############################################################################
# Step: Look at fingerprints to determine hierarchy. Insert entries into trie.#
###############################################################################

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
        return 'KreoClass-' + self.uuid[0:5] + '@' + (hex(self.fingerprint[-1].address + baseAddr) if len(self.fingerprint) > 0 else 'foobar')

trie = pygtrie.StringTrie()
methodToKreoClassMap = dict()  # we need a way to know which trie nodes correspond to each method, so that if a method gets mapped to multiple places we can reassign it to the LCA. Will need to make this more robust if we eventually decide to do some rearrangements or deletions from the trie before processing.

def constructTrie():
    global trie
    global traces

    for trace in traces:
        # Insert class and any parents into the trie
        for i in range(len(trace.fingerprint)):
            partialFingerprint = trace.fingerprint[0:i + 1]
            if KreoClass.fingerprintStr(partialFingerprint) not in trie.keys():
                newCls = KreoClass(partialFingerprint)
                trie[KreoClass.fingerprintStr(partialFingerprint)] = newCls

        cls = trie[KreoClass.fingerprintStr(trace.fingerprint)]

        for method in trace.methods():
            if method not in methodToKreoClassMap:
                methodToKreoClassMap[method] = set()
            
            methodToKreoClassMap[method].add(cls)
runStep(constructTrie, 'constructing trie...', 'trie constructed')

def reorganizeTrie():
    global trie
    global methodToKreoClassMap

    for method, clsSet in methodToKreoClassMap.items():
        assert(len(clsSet) >= 1)

        if len(clsSet) == 1:
            continue

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
        for cls in clsSet:
            clsTraces.append(trie._get_node(KreoClass.fingerprintStr(cls.fingerprint)))

        sharedTrace = findLongestSharedTrace(clsTraces)

        if len(sharedTrace) > 1:
            # LCA exists
            methodToKreoClassMap[method] = set([sharedTrace[-1][1].value])
        else:
            # LCA doesn't exist, have to add class to trie
            
            # Create class that will be inserted into the trie. This
            # class must have a new fingerprint that is the method
            # that will be assigned to the class that the two
            # classes share.
            newClassFingerprint = [method]
            newClass = KreoClass(newClassFingerprint)

            trie[KreoClass.fingerprintStr(newClassFingerprint)] = newClass

            # Move method to new class in methodToKreoClassMap
            methodToKreoClassMap[method] = set([newClass])

            # Remove from trie all classes that are being moved and update each class's fingerprint, then reinsert into trie
            for cls in clsSet:
                trie.pop(KreoClass.fingerprintStr(cls.fingerprint))
                cls.fingerprint = newClass.fingerprint + cls.fingerprint
                trie[KreoClass.fingerprintStr(cls.fingerprint)] = cls

    # Each method is now associated with exactly one class
    for clsSet in methodToKreoClassMap.values():
        assert(len(clsSet) == 1)
runStep(reorganizeTrie, 'reorganizing trie...', 'trie reorganized')

def swimDestructors():
    global trie
    # Move destructor functions into correct location in the trie. There is the
    # possibility that a parent object was never constructed but a child was. In
    # this case we know the destructor belongs to the parent but it currently
    # belongs to the child.
    for key in trie:
        # Find fingerprint[-1] in methodToKreoClassMap and replace the reference
        # with this class
        node = trie[key]
        if node.fingerprint[-1] in methodToKreoClassMap:
            methodToKreoClassMap[node.fingerprint[-1]] = set([node])
runStep(swimDestructors, 'moving destructors up in trie...', 'destructors moved up')

kreoClassToMethodSetMap: Dict[KreoClass, Set[Method]] = dict()

def mapTrieNodesToMethods():
    global methodToKreoClassMap
    global kreoClassToMethodSetMap
    # map trie nodes to methods now that method locations are fixed
    for method, trieNode in methodToKreoClassMap.items():
        trieNode = list(trieNode)[0]
        if trieNode not in kreoClassToMethodSetMap.keys():
            kreoClassToMethodSetMap[trieNode] = set()
        kreoClassToMethodSetMap[trieNode].add(method)
runStep(mapTrieNodesToMethods, 'mapping trie nodes to methods...', 'trie nodes mapped')

indent = ''
def print_trie(path_conv, path, children, cls=None):
    global indent
    if cls is None:
        print(f'{indent}n/a')
    else:
        path_c = path_conv(path)
        lastFingerprintMethod = cls.fingerprint[-1]
        print(f'{indent}{path_c} {str(cls)} {lastFingerprintMethod.isDestructor()} {lastFingerprintMethod.seenInHead} {lastFingerprintMethod.seenInFingerprint} {lastFingerprintMethod.seenInTorso()}')
        if cls in kreoClassToMethodSetMap:
            for method in kreoClassToMethodSetMap[cls]:
                print(f'{indent}* {method}')

    indent += '    '
    list(children)
    indent = indent[0:-4]
trie.traverse(print_trie)

# Output json in OOAnalyzer format

# copied from https://stackoverflow.com/a/3431838/1233320
def md5File(fname):
    hash_md5 = hashlib.md5()
    with open(fname, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

structures: Dict[str, Dict[str, Any]] = dict()
for trieNode in trie:
    node, trace = trie._get_node(trieNode)
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
    if cls in kreoClassToMethodSetMap:
        for method in kreoClassToMethodSetMap[cls]:
            method.updateType()
            methodAddrStr = hex(method.address + baseAddr)
            methods[methodAddrStr] = {
                'demangled_name': findMethodName(method) if findMethodName(method) != None else '',
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
print('Done, Kreo exiting normally.')
