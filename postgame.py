import json
import itertools
import uuid
import hashlib
import os
from parseconfig import config

procedures = dict() # map from address to procedure

class Procedure:
    def __init__(self, address, name='N/A'):
        self.address = address
        self.name = name
        # How many times the procedure has been seen in different parts of a trace:
        self.seenTail = 0
        self.seenTorso = 0

    def isDestructor(self):
        return self.seenTail > 0 and self.seenTorso <= self.seenTail//4 # TODO: more thought into this, other heuristics?

def findProcedure(address, name='N/A'):
    if address not in procedures:
        procedures[address] = Procedure(address, name)
    return procedures[address]

class TraceEntry:
    def __init__(self, line):
        splitLine = line.split()
        if len(splitLine) == 2:
            self.procedure = findProcedure(int(splitLine[0]))
            self.isCall = int(splitLine[1]) == 1
        elif len(splitLine) == 3:
            self.procedure = findProcedure(int(splitLine[1]), splitLine[0])
            self.isCall = int(splitLine[2]) == 1
        else:
            raise Exception('Could not parse trace entry from line: "' + line + '"')

    def __str__(self):
        procStr = ('' if self.procedure.name == 'N/A' else (self.procedure.name + ' ')) + str(self.procedure.address)
        return procStr + ' ' + ('1' if self.isCall else '0') + '\n'

    def __eq__(self, other):
        return self.procedure is other.procedure and self.isCall == other.isCall

class Trace:
    def __init__(self, traceEntries):
        self.traceEntries = traceEntries
        # Tail is in reverse order -- last call in the trace first
        self.tail = list(map(lambda entry: entry.procedure, itertools.takewhile(lambda entry: not entry.isCall, reversed(traceEntries))))
        self.numUpperBodyEntries = len(traceEntries) - len(self.tail)

    def __str__(self):
        return ''.join(map(lambda te: te.__str__(), self.traceEntries))

    def __hash__(self):
        return hash(self.__str__())

    # Return a generator for entries preceding the tail
    def torso(self):
        return itertools.islice(self.traceEntries, self.numUpperBodyEntries)

    # update all procedures involved with this trace, to store number of appearances in each part of the trace.
    def updateProcedureStatistics(self):
        for torsoEntry in self.torso():
            if not torsoEntry.isCall: # avoid double-counting, and avoid counting the calls corresponding to the returns in the tail
                torsoEntry.procedure.seenTorso += 1
        for tailProcedure in self.tail:
            tailProcedure.seenTail += 1

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
                if entry.procedure in destructors:
                    isTerminatingTrace = True
        # end of loop
        if curResultTrace:
            result.append(curResultTrace)
        return map(Trace, result)

def printTraces(traces):
    for trace in traces:
        print(trace)

# Step 1: Read traces from disk

def parseTraces():
    curTrace = []
    traces = []
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
    return traces
traces = parseTraces()

# Remove duplicate traces. TODO: check that the hash function that set() uses is appropriate.
def removeDuplicateTraces():
    global traces
    result = []
    tracesSet = set()
    for trace in traces:
        if not trace in tracesSet:
            tracesSet.add(trace)
            result.append(trace)
    traces = result
removeDuplicateTraces()

# Step 2: Record how many times each procedure was seen in each part of the trace.

for trace in traces:
    trace.updateProcedureStatistics()

# Step 3: Decide what's a destructor
destructors = set(filter(lambda proc: proc.isDestructor(), procedures.values()))

# Step 4: Split traces based on destructors
splitTraces = []
for trace in traces:
    splitTraces += trace.split(destructors)

# TODO: Possible improvement: Perform the last steps iteratively. I.e., if splitting reveals a new destructor, then we may want to re-split. However, splitting seems like it shouldn't be happening too often, let alone re-splitting.

# TODO: possible improvement: Instead of just looking for the true tail of returns, maybe if we identify a suffix common to many object traces, we can infer that a destructor is calling another method and account for it?

# Step 5: Look at tails to determine hierarchy

def identity(x):
    return x

def findIf(target, lst, key=identity):
    # Return list element equal to target after calling key on element
    for elt in lst:
        if target == key(elt):
            return elt
    return None

class KreoClass:
    def __init__(self, destructor):
        self.uuid = str(uuid.uuid4())
        self.destructor = destructor

    def __str__(self):
        return 'KreoClass-' + self.uuid[0:5] + '@' + str(self.destructor.address)

class TreeNode:
    def __init__(self, value=None, parent=None):
        self.value = value
        self.parent = parent
        self.children = []

    def insert(self, childValue):
        # Unconditionally insert into children list.
        self.children.append(TreeNode(childValue, self))

    def getPath(self, path, key=identity, mkNew=None):
        if path == []:
            return self
        step = path[0]
        matchingChild = findIf(step, self.children, lambda node: key(node.value))
        if matchingChild == None:
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

    # Generator gives tree starting from this node in preorder
    def preorderIter(self):
        yield self
        for child in self.children:
            for item in child.preorderIter():
                yield item

    def __str__(self):
        result = self.value.__str__() + '\nChildren: [\n'
        for child in self.children:
            result += child.__str__() + '\n'
        result += ']'
        return result

# Lowest common ancestor of two nodes
def treeNodesLCA(n1, n2):
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

treeRootNode = TreeNode(None, None)
methodNodes = dict() # we need a way to know which tree nodes correspond to each method, so that if a method gets mapped to multiple places we can reassign it to the LCA. Will need to make this more robust if we eventually decide to do some rearrangements or deletions from the tree before processing.
for trace in traces:
    # insert class into the tree if necessary
    classNode = treeRootNode.getPath(trace.tail, lambda cls: cls.destructor, KreoClass)

    # set LCAs if necessary
    for entry in trace.torso():
        procedure = entry.procedure
        oldMethodClassNode = methodNodes.get(procedure, treeRootNode)
        methodNodes[procedure] = treeNodesLCA(classNode, oldMethodClassNode)

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

structures = dict()
treeIter = treeRootNode.preorderIter()
next(treeIter) # skip the root
for classNode in treeIter:
    cls = classNode.value
    name = str(cls)

    # For now, while we only detect direct parent relationships, only add a member if we have a parent, and don't actually know anything about its size
    members = dict()
    if not classNode.parent is treeRootNode:
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

    structures[str(cls)] = {
        'name': name,
        'demangled_name': 'unknown', # TODO: use RTTI to get this if possible?
        'size': 4,
        'members': members,
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
