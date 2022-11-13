import json
import itertools
import uuid
import hashlib
import os
import time
from parseconfig import config
from typing import List, Callable, Dict, Set, Any

class Method:
    def __init__(self, address: int, name: str='N/A'):
        self.address = address
        self.name = name
        # How many times the method has been seen in different parts of a trace:
        self.seenInTail = int(0)
        self.seenInTorso = int(0)

    def isDestructor(self) -> bool:
        return self.seenInTail > 0 and self.seenInTorso <= self.seenInTail // 4  # TODO: more thought into this, other heuristics?

    def __str__(self) -> str:
        return ('' if self.name == 'N/A' else (self.name + ' ')) + str(self.address)

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

class Trace:
    def __init__(self, traceEntries: List[TraceEntry]):
        self.traceEntries = traceEntries
        # Tail is in reverse order -- last call in the trace first, aka fingerprint
        self.tail = list(map(lambda entry: entry.method, itertools.takewhile(lambda entry: not entry.isCall, reversed(traceEntries))))
        self.torsoLen = len(traceEntries) - len(self.tail)

    def __str__(self) -> str:
        return '\n'.join(map(lambda te: te.__str__(), self.traceEntries))

    def __hash__(self):
        return hash(self.__str__())

    def __eq__(self, other) -> bool:
        return self.__str__() == other.__str__()

    # Return a generator for entries preceding the tail
    def torso(self):
        return itertools.islice(self.traceEntries, self.torsoLen)

    # update all methods involved with this trace, to store number of appearances in each part of the trace.
    def updateMethodStatistics(self):
        for torsoEntry in self.torso():
            if not torsoEntry.isCall: # avoid double-counting, and avoid counting the calls corresponding to the returns in the tail
                torsoEntry.method.seenInTorso += 1
        for tailMethod in self.tail:
            tailMethod.seenInTail += 1

    # Given a set of destructors, return a list of traces created from this one. Returns just itself if no splitting is necessary.
    def split(self, destructors):
        # basically a state machine as we iterate through the entries. When we find a destructor, switch to a state where we're essentially trying to end the sub-trace as soon as we can.
        result = []
        curResultTrace = []
        isTerminatingTrace = False
        for entry in self.traceEntries:
            if isTerminatingTrace:
                if entry.isCall: # the tail is over, rejoice or something
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

def findOrInsertMethod(address: int, name: str='N/A') -> Method:
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

traces: List[Trace] = []
runStep(parseTraces, 'parsing traces...', f'traces parsed')
print(f'found {len(traces)} traces')

# Remove duplicate traces.
def removeDuplicateTraces():
    global traces
    tracesSet: Set[Trace] = set()
    for trace in traces:
        tracesSet.add(trace)
    traces = list(tracesSet)

runStep(removeDuplicateTraces, 'removing duplicates...', f'duplicates removed')
print(f'now are {len(traces)} unique traces')

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

# TODO: possible improvement: Instead of just looking for the true tail of returns, maybe if we identify a suffix common to many object traces, we can infer that a destructor is calling another method and account for it?

# Step 5: Look at tails to determine hierarchy

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
        return 'KreoClass-' + self.uuid[0:5] + '@' + str(self.destructor.address)

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

    # TODO this doesn't work
    def getPath(self, path: List[Method], key: Callable[[Any], Any]=identity, mkNew: Callable[[Method], Any]=None):
        '''
        Attempts to insert the given path into the TrieNode entry.
        '''
        if path == []:
            return self

        step = path[0]

        # Attempt to find the class in the TrieNode's child.
        matchingChild = findIf(step, self.children, lambda node: key(node.value))
        if matchingChild == None:
            # Class not in TrieNode's child, make a new child node and insert it (add it as a child)
            if not mkNew:
                raise Exception('Path not found and mkNew not provided')
            newChildValue = mkNew(step)
            self.insert(newChildValue)

        return self.getPath(path[1:], key, mkNew)

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

trieRootNode = TrieNode(None, None)
methodToTrieNodeMap: Dict[Method, TrieNode] = dict()  # we need a way to know which trie nodes correspond to each method, so that if a method gets mapped to multiple places we can reassign it to the LCA. Will need to make this more robust if we eventually decide to do some rearrangements or deletions from the trie before processing.

def constructTrie():
    global trieRootNode
    global methodToTrieNodeMap
    for trace in traces:
        # insert class into the trie if necessary
        trieNode = trieRootNode.getPath(trace.tail, lambda cls: cls.destructor, KreoClass)
        print(trieNode == trieRootNode)
        print(str(trieNode))

        # set LCAs if necessary
        # TODO this is wrong since getPath always returns the root
        for entry in trace.torso():
            method = entry.method
            existingTrieNodeForMethod = methodToTrieNodeMap.get(method, None)
            methodClassNodeLCA = trieNode if existingTrieNodeForMethod is None else trieNodesLCA(trieNode, existingTrieNodeForMethod)
            methodToTrieNodeMap[method] = methodClassNodeLCA

        # for method in methodToTrieNodeMap:
        #     print(str(method) + " ==> " + str(methodToTrieNodeMap[method]))

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

structures: Dict[str, Dict[str, Any]] = dict()
trieIter = trieRootNode.preorderIter()
next(trieIter) # skip the root
for classNode in trieIter:
    cls = classNode.value
    name = str(cls)

    # For now, while we only detect direct parent relationships, only add a member if we have a parent, and don't actually know anything about its size
    # We are not detecting members so members should always be empty
    members = dict()
    if not classNode.parent is trieRootNode:
        parentClass = classNode.parent.value
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

    methods: Dict[str, Any] = dict()

    structures[str(cls)] = {
        'name': name,
        'demangled_name': 'unknown', # TODO: use RTTI to get this if possible?
        'size': 4,
        'members': members,
        'methods': methods,
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
