import json
import itertools
import uuid
import hashlib
import os
import time
import pygtrie
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
        self.destructorTailToHeadRatioMax = 16
        self.constructorHeadToTorsoRatioMax = 4
        self.constructorHeadToTailRatioMax = 16

    def seenInTorso(self):
        return self.seenTotal - self.seenInHead - self.seenInFingerprint

    def isDestructor(self) -> bool:
        return self.seenInFingerprint > 0
        # return (self.seenInFingerprint > 0
        #         and self.seenInTorso() <= self.seenInFingerprint // self.destructorTailToTorsoRatioMax
        #         and self.seenInHead <= self.seenInFingerprint // self.destructorTailToHeadRatioMax) # TODO: more thought into this, other heuristics?

    def isConstructor(self) -> bool:
        return self.seenInHead > 0
        # return (self.seenInHead > 0
        #         and self.seenInTorso() <= self.seenInHead // self.constructorHeadToTorsoRatioMax
        #         and self.seenInFingerprint <= self.seenInHead // self.constructorHeadToTailRatioMax) # TODO maybe improve

    def evaluateType(self) -> None:
        # TODO other types may be viable options (virtual methods for example), but for now we don't care about them
        # assert not (self.isConstructor() and self.isDestructor())
        if self.isDestructor():
            self.type = 'dtor'
        elif self.isConstructor():
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

    # update all methods involved with this trace, to store number of appearances in each part of the trace.
    def updateMethodStatistics(self):
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
        return map(lambda entry: entry.method, filter(lambda entry: entry.isCall, self.traceEntries))

    def split(self, constructors: Set[Method], destructors: Set[Method]):
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
            if currEntry.method.isDestructor() and not currEntry.isCall:
                # Iterate until curr entry not destructor
                while currEntry is not None and currEntry.method.isDestructor():
                    currEntry = iterateAndInsert()

                # If curr entry is a constructor, split the trace
                if currEntry is not None and currEntry.method.isConstructor():
                    # NOTE: must move currEntry from the current trace to the new currTrace
                    splitTraces.append(currTrace[0:-1])
                    currTrace = [currEntry]

                # Otherwise don't split the trace
            currEntry = iterateAndInsert()

        if currTrace != []:
            splitTraces.append(currTrace)

        for trace in splitTraces:
            assert (len(trace) > 0)
            for entry in trace:
                assert(entry is not None)

        return map(Trace, splitTraces)

methods: Dict[int, Method] = dict() # map from address to method
methodNames: Dict[Method, str] = dict()

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

# Step 1: Read traces from disk

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
            curTrace.append(TraceEntry(line))
    # finish the last trace
    if curTrace:
        traces.append(Trace(curTrace))

    for line in open(config['objectTracesPath'] + '-name-map'):
        splitlines = line.split()
        insertMethodName(int(splitlines[0]), splitlines[1])
    
    for line in open(config['gtMethodsPath']):
        gtMethods.add(int(line))

traces = []
runStep(parseTraces, 'parsing traces...', f'traces parsed')
print(f'found {len(traces)} traces')

# Step 2: Record how many times each method was seen in the head and fingerprint

def updateAllMethodStatistics():
    global traces
    global methods
    for method in methods.values():
        method.seenInHead = 0
        method.seenInFingerprint = 0
        method.seenTotal = 0
    for trace in traces:
        trace.updateMethodStatistics()

runStep(updateAllMethodStatistics, 'updating method statistics...', 'method statistics updated')

# Step 3: Decide what's a constructor/destructor

destructors: Set[Method] = set()
constructors: Set[Method] = set()
def findConstructorsDestructors():
    global destructors
    global constructors
    global methods
    destructors = set(filter(lambda method: method.isDestructor(), methods.values()))
    constructors = set(filter(lambda method: method.isConstructor(), methods.values()))

runStep(findConstructorsDestructors,
    'finding constructors/destructors for each object trace...',
    'constructors/destructors found for each object trace')

# Step 4: Split spurious traces
def splitTracesFn():
    global traces
    splitTraces: List[Trace] = []
    for trace in traces:
        splitTraces += trace.split(constructors, destructors)
    traces = splitTraces
runStep(splitTracesFn, 'splitting traces...', f'traces split')
print(f'after splitting there are now {len(traces)} traces')

# Note: splitting traces will not reveal new constructors/destructors; however, we do need to recompute the statistics
runStep(updateAllMethodStatistics, 'updating method statistics again...', 'method statistics updated')

# Remove duplicate traces after splitting
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

# TODO: possible improvement: Instead of just looking for the true fingerprint
# of returns, maybe if we identify a suffix common to many object traces, we can
# infer that a destructor is calling another method and account for it?

# Don't have to do this since any element in the fingerprint is considered a destructor currently
# Step: Update fingerprints to only include methods that are destructors
# def removeNondestructorsFromFingerprints():
#     global destructors
#     global methods
#     global traces
#     for trace in traces:
#         trace.fingerprint = [method for method in trace.fingerprint if method in destructors]
#     # Remove traces with empty fingerprints
#     traces = list(filter(lambda trace: len(trace.fingerprint) > 0, traces))
# runStep(removeNondestructorsFromFingerprints,
#     'removing methods that aren\'t destructors from fingerprints...',
#     'method removed')

# Step 5: Look at fingerprints to determine hierarchy

class KreoClass:
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
        return 'KreoClass-' + self.uuid[0:5] + '@' + (hex(self.fingerprint[-1].address + baseAddr) if len(self.fingerprint) > 0 else 'foobar')

# Ideally, everything would form a nice trie. But what if it doesn't, eg in the case when we're just tracing weird sequence of functions that operate on an integer pointer? The simple algorithm of always inserting into the trie will never error out, but it will result in a trie where certain "destructors" are at the base of some tries and in the middle of others. So that's a rule we could use to exclude some of them: A class that appears in multiple different branches from the root should be removed
# ^^^ But in fact, this happens automatically! Because the LCA will be the root, and we skip the root when we traverse over the tree to print final output!

trie = pygtrie.StringTrie()
methodToKreoClassMap = dict()  # we need a way to know which trie nodes correspond to each method, so that if a method gets mapped to multiple places we can reassign it to the LCA. Will need to make this more robust if we eventually decide to do some rearrangements or deletions from the trie before processing.

kreoClassToMethodSetMap: Dict[KreoClass, Set[Method]] = dict()

def constructTrie():
    global trieRootNode
    global methodToKreoClassMap
    for trace in traces:
        # Insert class and any parents into the trie
        for i in range(len(trace.fingerprint)):
            partialFingerprint = trace.fingerprint[0:i + 1]
            if KreoClass.fingerprintStr(partialFingerprint) not in trie.keys():
                trie[KreoClass.fingerprintStr(partialFingerprint)] = KreoClass(partialFingerprint)

        cls = trie[KreoClass.fingerprintStr(trace.fingerprint)]

        for method in trace.methods():
            if method not in methodToKreoClassMap:
                methodToKreoClassMap[method] = cls
            else:
                clsInMethodMap: KreoClass = methodToKreoClassMap[method]

                if KreoClass.fingerprintStr(cls.fingerprint) == KreoClass.fingerprintStr(clsInMethodMap.fingerprint):
                    # We inserted the object-trace into the same location as a preexisting
                    # object-trace, so simply add the method to the map.
                    methodToKreoClassMap[method] = cls
                else:
                    # find least common ancestor since entry in map is different than the
                    # newly added trie class

                    _, clsInMethodMapTrace = trie._get_node(KreoClass.fingerprintStr(clsInMethodMap.fingerprint))
                    _, clsNodeTrace = trie._get_node(KreoClass.fingerprintStr(cls.fingerprint))

                    def findLongestSharedTrace(t1, t2):
                        for i in range(min(len(t1), len(t2))):
                            t1Node = t1[i]
                            t2Node = t2[i]
                            if t1Node != t2Node:
                                return t1[:i]
                        if len(t1) > len(t2):
                            return t2
                        else:
                            return t1

                    sharedTrace = findLongestSharedTrace(clsInMethodMapTrace, clsNodeTrace)
                    if sharedTrace == clsInMethodMapTrace:
                        # Do nothing since the method's already in the parent class
                        pass
                    elif sharedTrace == clsNodeTrace:
                        # Reinsert methods since new class is shared ancestor
                        methodToKreoClassMap[method] = cls
                    elif len(sharedTrace) > 1:
                        methodToKreoClassMap[method] = sharedTrace[-1][1].value
                    else:
                        print(f'classes {str(cls)} and {clsInMethodMap} are not common ancestors yet they share method {str(method)}')
                        # TODO currently not worth creating another class because of how many false positive methods there are
                        # that would cause the trie to do some weird stuff.
                        pass
        
runStep(constructTrie, 'constructing trie...', 'trie constructed')

# Step 6: Associate methods from torsos with classes
# Step 7: Static dataflow analysis

# Output json in OOAnalyzer format

# copied from https://stackoverflow.com/a/3431838/1233320
def md5File(fname):
    hash_md5 = hashlib.md5()
    with open(fname, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()

# Move destructor functions into correct location in the trie. There is the
# possibility that a parent object was never constructed but a child was. In
# this case we know the destructor belongs to the parent but it currently
# belongs to the child.
for key in trie:
    # Find fingerprint[-1] in methodToKreoClassMap and replace the reference
    # with this class
    node = trie[key]
    if node.fingerprint[-1] in methodToKreoClassMap:
        methodToKreoClassMap[node.fingerprint[-1]] = node

# map trie nodes to methods now that method locations are fixed
for method, trieNode in methodToKreoClassMap.items():
    if trieNode not in kreoClassToMethodSetMap.keys():
        kreoClassToMethodSetMap[trieNode] = set()
    kreoClassToMethodSetMap[trieNode].add(method)

indent = ''
def print_trie(path_conv, path, children, cls=None):
    global indent
    path_c = path_conv(path)
    if path_c == '':
        print(indent + 'n/a')
    else:
        print(indent + str(path_c) + ' ' + str(cls.fingerprint[-1].isDestructor()))
        if cls in kreoClassToMethodSetMap:
            for method in kreoClassToMethodSetMap[cls]:
                print(indent + '* ' + str(method))

    indent += '    '
    list(children)
    indent = indent[0:-4]
trie.traverse(print_trie)

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
            method.evaluateType()
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
