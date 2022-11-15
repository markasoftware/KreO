import json
import itertools
import uuid
import hashlib
import os
import time
from parseconfig import config
from typing import List, Callable, Dict, Set, Any

baseAddr = 0x400000

class Method:
    def __init__(self, address: int, name: str='unknown'):
        self.address = address
        self.name = name
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
        return (self.seenInFingerprint > 0
                and self.seenInTorso() <= self.seenInFingerprint // self.destructorTailToTorsoRatioMax
                and self.seenInHead <= self.seenInFingerprint // self.destructorTailToHeadRatioMax) # TODO: more thought into this, other heuristics?

    def isConstructor(self) -> bool:
        return (self.seenInHead > 0
                and self.seenInTorso() <= self.seenInHead // self.constructorHeadToTorsoRatioMax
                and self.seenInFingerprint <= self.seenInHead // self.constructorHeadToTailRatioMax) # TODO maybe improve

    def evaluateType(self) -> None:
        # TODO other types may be viable options (virtual methods for example), but for now we don't care about them
        assert not (self.isConstructor() and self.isDestructor())
        if self.isDestructor():
            self.type = 'dtor'
        elif self.isConstructor():
            self.type = 'ctor'
        else:
            self.type = 'meth'

    def __str__(self) -> str:
        return ('' if self.name == 'unknown' else (self.name + ' ')) +\
               (str(self.address)) +\
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
        elif len(splitLine) == 3:
            # There's a method name in the trace
            self.method = findOrInsertMethod(int(splitLine[1]), splitLine[0])
            self.isCall = int(splitLine[2]) == 1
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

    def __str__(self) -> str:
        return '\n'.join(map(lambda te: te.__str__(), self.traceEntries))

    def __hash__(self):
        return hash(self.__str__())

    def __eq__(self, other) -> bool:
        return self.__str__() == other.__str__()

    # update all methods involved with this trace, to store number of appearances in each part of the trace.
    def updateMethodStatistics(self):
        for torsoEntry in self.traceEntries:
            if not torsoEntry.isCall: # avoid double-counting, and avoid counting the calls corresponding to the returns in the fingerprint
                torsoEntry.method.seenTotal += 1
        for headMethod in self.head:
            headMethod.seenInHead += 1
        for fingerprintMethod in self.fingerprint:
            fingerprintMethod.seenInFingerprint += 1

    def methods(self):
        return map(lambda entry: entry.method, filter(lambda entry: entry.isCall, self.traceEntries))

    # Given a set of destructors, return a list of traces created from this one. Returns just itself if no splitting is necessary.
    def split(self, destructors):
        # basically a state machine as we iterate through the entries. When we find a destructor, switch to a state where we're essentially trying to end the sub-trace as soon as we can.
        result = []
        curResultTrace = []
        isTerminatingTrace = False
        for entry in self.traceEntries:
            if isTerminatingTrace:
                if entry.isCall: # the fingerprint is over, rejoice or something
                    result.append(curResultTrace)
                    curResultTrace = [entry]
                    isTerminatingTrace = False
                else:
                    curResultTrace.append(entry)
            else: # not terminating the trace
                curResultTrace.append(entry)
                if entry.method in destructors:
                    isTerminatingTrace = True
        # end of loop
        if curResultTrace:
            result.append(curResultTrace)
        return map(Trace, result)

methods: Dict[int, Method] = dict() # map from address to method

def findOrInsertMethod(address: int, name: str='unknown') -> Method:
    '''
    Attempts to find the method in the global methods map. If the function fails
    to find a method, one will be inserted.
    '''
    global methods

    if address not in methods:
        methods[address] = Method(address, name)
    return methods[address]

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

def parseTraces():
    global traces

    curTrace: List[TraceEntry] = []
    # there can be multiple object trace files...find all of them

    traceIndex = 0
    while os.path.exists(config['objectTracesPath'] + "_" + str(traceIndex)):
        for line in open(config['objectTracesPath'] + "_" + str(traceIndex)):
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

        traceIndex += 1

traces = []
runStep(parseTraces, 'parsing traces...', f'traces parsed')
print(f'found {len(traces)} traces')

# Remove duplicate traces.
def removeDuplicateTraces():
    global traces
    tracesSet: Set[Trace] = set()
    for trace in traces:
        if trace in tracesSet:
            print(trace.traceEntries[0].method)
        tracesSet.add(trace)
    traces = list(tracesSet)

runStep(removeDuplicateTraces, 'removing duplicates...', f'duplicates removed')
print(f'now are {len(traces)} unique traces')

with open('out/object-traces-no-duplicates', 'w') as f:
    for trace in traces:
        for entry in trace.traceEntries:
            f.write(str(entry) + '\n')

        f.write('\n')

# Step 2: Record how many times each method was seen in each part of the trace.

def updateAllMethodStatistics():
    global traces
    for trace in traces:
        trace.updateMethodStatistics()

runStep(updateAllMethodStatistics, 'updating method statistics...', 'method statistics updated')

# Step 3: Decide what's a destructor

destructors: Set[Method] = set()
def findDestructors():
    global destructors
    destructors = set(filter(lambda method: method.isDestructor(), methods.values()))
runStep(findDestructors, 'finding destructors for each object trace...', 'destructors found for each boject trace')

# Step 4: Split traces based on destructors
splitTraces: List[Trace] = []
def splitTracesFn():
    global splitTraces
    for trace in traces:
        splitTraces += trace.split(destructors)
runStep(splitTracesFn, 'splitting traces...', f'traces split')
print(f'after splitting there are now {len(splitTraces)} traces')
traces = splitTraces

# TODO: Possible improvement: Perform the last steps iteratively. I.e., if splitting reveals a new destructor, then we may want to re-split. However, splitting seems like it shouldn't be happening too often, let alone re-splitting.

# TODO: possible improvement: Instead of just looking for the true fingerprint of returns, maybe if we identify a suffix common to many object traces, we can infer that a destructor is calling another method and account for it?

# Step 5: Look at fingerprints to determine hierarchy

def identity(x):
    return x

def findIf(target: Any, lst: List[Any], key: Callable[[Any], Any]=identity) -> Any:
    # Return list element equal to target after calling key on element
    for elt in lst:
        if target == key(elt):
            return elt
    return None

class KreoClass:
    def __init__(self, destructor: Method):
        self.uuid = str(uuid.uuid4())
        self.destructor = destructor

    def __str__(self) -> str:
        return 'KreoClass-' + self.uuid[0:5] + '@' + hex(self.destructor.address + baseAddr)

class TrieNode:
    def __init__(self, value: Any=None, parent=None):
        self.value = value
        self.parent = parent
        self.children: List[TrieNode] = []

    def __str__(self) -> str:
        retStr: str = ('Root' if self.value is None else str(self.value)) + ': {'
        for child in self.children:
            retStr += str(child) + ', '
        return retStr.strip(' ,') + '}' 

    def size(self) -> int:
        if self.children == []:
            return int(1)

        childSize = int(0)
        for child in self.children:
            childSize += child.size()
        return childSize

    def insert(self, childValue: Any) -> None:
        # Unconditionally insert into children list.
        self.children.append(TrieNode(childValue, self))
        return self.children[-1]

    def insertTraceIntoTrie(self, path: List[Method], key: Callable[[Any], Any]=identity, mkNew: Callable[[Method], Any]=None):
        '''
        Attempts to insert the given path into the TrieNode entry.
        '''
        if path == []:
            return self

        step = path[0]

        # Attempt to find the class in the TrieNode's child.
        matchingChild: TrieNode = findIf(step, self.children, lambda node: key(node.value))

        if matchingChild == None:
            # Class not in TrieNode's child, make a new child node and insert it (add it as a child)
            if not mkNew:
                raise Exception('Path not found and mkNew not provided')
            newChildValue = mkNew(step)
            matchingChild = self.insert(newChildValue)

        return matchingChild.insertTraceIntoTrie(path[1:], key, mkNew)

    def isRoot(self):
        return self.parent == None

    # Iterator of parent nodes up to and including this node, root node first.
    def breadcrumbs(self):
        reversedBcs = [self]
        curNode = self
        while curNode.parent:
            curNode = curNode.parent
            reversedBcs.append(curNode)
        return reversed(reversedBcs)

    # Generator gives trie starting from this node in preorder
    def preorderIter(self):
        yield self
        for child in self.children:
            for item in child.preorderIter():
                yield item

# Lowest common ancestor of two nodes
def trieNodesLCA(n1: TrieNode, n2: TrieNode) -> TrieNode:
    n1bcs = n1.breadcrumbs()
    n2bcs = n2.breadcrumbs()
    n1root = next(n1bcs)
    n2root = next(n2bcs)
    assert n1root is n2root, "Tried to find LCA of two nodes with different roots."
    lastAncestor = n1root
    while True:
        try:
            n1n = next(n1bcs)
            n2n = next(n2bcs)
        except StopIteration:
            return lastAncestor

        if n1n is n2n:
            lastAncestor = n1n
        else:
            return lastAncestor

# Ideally, everything would form a nice trie. But what if it doesn't, eg in the case when we're just tracing weird sequence of functions that operate on an integer pointer? The simple algorithm of always inserting into the trie will never error out, but it will result in a trie where certain "destructors" are at the base of some tries and in the middle of others. So that's a rule we could use to exclude some of them: A class that appears in multiple different branches from the root should be removed
# ^^^ But in fact, this happens automatically! Because the LCA will be the root, and we skip the root when we traverse over the tree to print final output!

trieRootNode = TrieNode(None, None)
methodToTrieNodeMap = dict()  # we need a way to know which trie nodes correspond to each method, so that if a method gets mapped to multiple places we can reassign it to the LCA. Will need to make this more robust if we eventually decide to do some rearrangements or deletions from the trie before processing.

def constructTrie():
    global trieRootNode
    global methodToTrieNodeMap
    for trace in traces:
        # insert class into the trie if necessary. A dummy first fingerprint element added
        # to the fingerprint for the root node
        trieNode = trieRootNode.insertTraceIntoTrie(trace.fingerprint, lambda cls: cls.destructor, KreoClass)

        # set LCAs if necessary
        for method in trace.methods():
            existingTrieNodeForMethod = methodToTrieNodeMap.get(method, None)
            methodClassNodeLCA = trieNode if existingTrieNodeForMethod is None else trieNodesLCA(trieNode, existingTrieNodeForMethod)
            if methodClassNodeLCA == trieRootNode:
                # the classes are related but we did not discover a class to
                # relate them, so create a new class and insert it in the trie
                # below the root, then move the branches that contain the two
                # nodes to be related to below the new trie node.

                # This brings up a problem though - you might not be able to
                # find a class if it has been moved. You could have another
                # class that you insert that has the same fingerprint as one
                # of the classes you moved, but you wouldn't be able to find
                # it since it moved.
                methodClassNodeLCA = trieNode

            methodToTrieNodeMap[method] = methodClassNodeLCA

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

# map trie nodes to methods now that method locations are fixed
trieNodeToMethodSetMap: Dict[TrieNode, Set[Method]] = dict()

for method, trieNode in methodToTrieNodeMap.items():
    if trieNode not in trieNodeToMethodSetMap.keys():
        trieNodeToMethodSetMap[trieNode] = set()
    trieNodeToMethodSetMap[trieNode].add(method)

structures: Dict[str, Dict[str, Any]] = dict()
trieIter = trieRootNode.preorderIter()
next(trieIter) # skip the root
for trieNode in trieIter:
    cls = trieNode.value
    name = str(cls)

    # For now, while we only detect direct parent relationships, only add a member if we have a parent, and don't actually know anything about its size
    # We are not detecting members so members should always be empty
    members = dict()
    if not trieNode.parent is trieRootNode:
        parentClass = trieNode.parent.value
        members['0x0'] = {
            'base': False, # TODO: what does this one even mean?
            'name': name + '_0x0',
            'offset': '0x0',
            'parent': True,
            'size': 4,
            'struc': str(parentClass),
            'type': 'struc',
            'usages': [],
        }

    methods = dict()
    # If there are no methods associated with the trie node there might not be any methods in the set
    if trieNode in trieNodeToMethodSetMap:
        for method in trieNodeToMethodSetMap[trieNode]:
            method.evaluateType()
            methodAddrStr = hex(method.address + baseAddr)
            methods[methodAddrStr] = {
                'demangled_name': method.name,
                'ea': methodAddrStr,
                'import': False,
                'name': method.type + "_" + methodAddrStr,
                'type': method.type,
            }

    structures[str(cls)] = {
        'demangled_name': '', # TODO: use RTTI to get this if possible?
        'name': name,
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
