#include <cassert>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "pin.H"

/// Uncomment these to log messages of various levels
#define LOG_INFO
#define LOG_WARN

using namespace std;

/// Max number of elements in the finished object traces map.
#define FINISHED_OBJECT_TRACES_MAX_SIZE 1'000

/// HACK: on Windows, Pintool uses STLP, which puts lots of the C++11 standard
/// library data structures into the tr1 namespace (as was normal pre-C++11).
/// Of course, this is fairly fragile because it depends on the exact
/// implementation Pin uses...but I doubt they'll change it in any way other
/// than eventually removing it and supporting C++11 on Windows
#ifdef _STLP_BEGIN_TR1_NAMESPACE
using namespace std::tr1;
#endif

/// malloc names -- not sure how correct it is, but it's how it's done in
/// malloctrace.c from pin examples
#ifdef TARGET_MAC
#define MALLOC_SYMBOL "_malloc"
#define FREE_SYMBOL "_free"
#else
#define MALLOC_SYMBOL "malloc"
#define FREE_SYMBOL "free"
#endif

/// Mangled delete operator (msvc-specific), not sure how applicable this is to
/// other compilers
#define DELETE_OPERATOR_SIZE "??3@YAXPAXI@Z"
#define DELETE_OPERATOR "??3@YAXPAX@Z"
vector<string> deleteOperators;

// ============================================================================
//                                    Utilities
// ============================================================================

using Tid = OS_THREAD_ID;

inline Tid GetTid() { return ::PIN_GetTid(); }

// ============================================================================
/// @brief An entry in an object trace.
class ObjectTraceEntry {
 public:
  ObjectTraceEntry(ADDRINT procedure, bool isCall)
      : procedure(procedure),
        isCall(isCall) {}

  // procedure's address
  ADDRINT procedure;

  // true indicates call, false indicates return
  bool isCall;

  // set of directly called procedures (this currently doesn't exist)
  // TODO currently creating unordered_sets
  // for each ObjectTraceEntry causes the application to run out of memory
  // (maybe because we are copying ObjectTraceEntries all over the place + sets
  // just take up a lot more memory than two literals).

  friend bool operator==(const ObjectTraceEntry& e1,
                         const ObjectTraceEntry& e2) {
    return e1.procedure == e2.procedure && e1.isCall == e2.isCall;
  }

  friend ostream& operator<<(ostream& os, const ObjectTraceEntry& o) {
    os << "{ 0x" << hex << o.procedure << ", " << dec << o.isCall << " }";
    return os;
  }
};

/// An object trace is a pointer to a list of ObjectTraceEntries.
using ObjectTrace = vector<ObjectTraceEntry>*;

// ============================================================================
/// @brief An entry in the shadow stack. A tuple of the form (object pointer,
/// expected return address, calledProcedures set). calledProcedures initially
/// empty.
struct ShadowStackEntry {
 public:
  ShadowStackEntry(ADDRINT objPtr, ADDRINT returnAddr, ADDRINT procedure)
      : objPtr(objPtr),
        returnAddr(returnAddr),
        procedure(procedure) {}

  ADDRINT objPtr;
  ADDRINT returnAddr;
  ADDRINT procedure;
};

// ============================================================================
//                                  Pin Knobs
// ============================================================================
KNOB<string> methodCandidatesPath(KNOB_MODE_WRITEONCE, "pintool",
                                  "method-candidates", "out/method-candidates",
                                  "Path to method candidates file.");

KNOB<string> gtMethodsPath(KNOB_MODE_WRITEONCE, "pintool", "gt-methods",
                           "out/gt-methods",
                           "Path to ground truth methods file.");

KNOB<string> gtMethodsInstrumentedPath(
    KNOB_MODE_WRITEONCE, "pintool", "gt-methods-instrumented",
    "out/gt-methods-instrumented",
    "Path to write list of instrumented ground truth methods");

KNOB<string> blacklistedMethodsPath(
    KNOB_MODE_WRITEONCE, "pintool", "blacklisted-methods",
    "out/blacklisted-methods", "Path to write list of blacklisted methods to");

KNOB<string> objectTracesPath(KNOB_MODE_WRITEONCE, "pintool", "object-traces",
                              "out/object-traces",
                              "Path to object traces output file.");

// ============================================================================
/// Potential method candidates, populated from the method candidates found
/// during pregame
unordered_set<ADDRINT> methodCandidateAddrs;

/// Ground truth methods that were instrumented. Only populated during
/// evaluation and not used by KreO itself, just to measure method coverage.
unordered_set<ADDRINT> gtMethodsInstrumented;

/// Ground truth method candidates, populated from ground truth method
/// candidates. Only used during evaluation and not used by KreO itself, just
/// used to measure method coverage.
unordered_set<ADDRINT> gtMethodAddrs;

/// Lock to manage active object traces. When registering callbacks using
/// RTN_InsertCall, the pin client lock is not acquired, so we must manage
/// our own lock.
::PIN_LOCK activeObjectTracesLock;

/// Current active object traces (those that are associated with objects that
/// are not deallocated). Map potential this-pointer to the object trace
/// associated with the pointer.
map<ADDRINT, ObjectTrace> activeObjectTraces;

/// Manages access to finishedObjectTraces.
::PIN_LOCK finishedObjectTracesLock;

/// List of object traces that are done.
vector<ObjectTrace> finishedObjectTraces;

/// Total number of object traces found (number includes those already
/// written to memory). This is a rough estimate that does not include
/// object-traces that are eliminated because they contain only blacklisted
/// procedures.
ADDRINT totalObjectTraces = 0;

/// Manages access to threadToShadowStackMap and the stackEntryCount.
::PIN_LOCK shadowStackLock;

/// Our own stack that we use to allow us to perform proper call return
/// matching. This is required because optimization may happen that results in
/// some calls not having matching returns. A shadow stack exists for each
/// thread.
map<Tid, vector<ShadowStackEntry>> threadToShadowStackMap;

/// number of times each return address appears in the method stack.
/// We don't need a stackEntryCount for each thread since each thread will
/// Contain a unique set of addresses in their appropriate stack entry. Access
/// should still be regulated by the shadowStackLock since unordered_map operations
/// are not atomic.
unordered_map<ADDRINT, int> stackEntryCount;

/// Lock that manages access to heap allocated regions.
::PIN_LOCK heapAllocatedRegionsLock;

/// Map starts of regions to ends of regions.
map<ADDRINT, ADDRINT> heapAllocatedRegions;

/// ^^^, but for mapped areas of memory (images). Used to determine valid object
/// ptrs.
map<ADDRINT, ADDRINT> mappedRegions;

/// Base offset of the executable image. Used to offset procedure addresses so
/// they are absolute and not relative to some offset base address of the loaded
/// in image.
ADDRINT lowAddr;

/// Regulates access to tidToStackBaseMap.
::PIN_LOCK stackBaseLock;

/// Maps thread ID to the base of the stack for the thread associated with
/// said ID.
map<Tid, ADDRINT> tidToStackBaseMap;

/// Set of blacklisted procedures. This set is saved when the pintool is
/// complete.
set<ADDRINT> blacklistedProcedures;

/// Can't use RTN methods during Fini, so we explicitly store all the symbol
/// names here during InstrumentImage (for debugging).
unordered_map<ADDRINT, string> procedureSymbolNames;

/// Regulates access to tidToMallocSize.
::PIN_LOCK tidToMallocSizeLock;

/// Maps thread ID to current malloc size recorded in a malloc observer
/// function.
map<Tid, ADDRINT> tidToMallocSize;

/// Regulates access to threadToLastRetAddrMap.
::PIN_LOCK threadToLastRetAddrLock;

/// Updated when call instruction instrumented. Stores the last return address
/// for each given thread.
map<Tid, ADDRINT> threadToLastRetAddrMap;

// ============================================================================
/// @brief Removes any methods that are blacklisted from the finished object
/// traces. Also removes the trace if the trace is empty after blacklisted
/// procedures are removed from it.
/// @note This does not guarantee that /all/ object-traces have all blacklisted
/// procedures removed because we periodically write object-traces to disk
/// before all blacklisted procedures have been identified. One should remove
/// all blacklisted methods saved prior to performing additional analysis.
void RemoveBlacklistedMethods() {
  Tid tid = GetTid();

  ::PIN_GetLock(&finishedObjectTracesLock, tid);

  for (auto traceIt = finishedObjectTraces.begin();
       traceIt != finishedObjectTraces.end();) {
    ObjectTrace trace = *traceIt;
    assert(trace != nullptr);

    for (auto entryIt = trace->begin(); entryIt != trace->end();) {
      if (blacklistedProcedures.count(entryIt->procedure)) {
        entryIt = trace->erase(entryIt);
      } else {
        entryIt++;
      }
    }

    if (trace->empty()) {
      // The trace contains not more entries (all were blacklisted), so remove
      // the trace.
      traceIt = finishedObjectTraces.erase(traceIt);
      delete trace;
    } else {
      traceIt++;
    }
  }

  ::PIN_ReleaseLock(&finishedObjectTracesLock);
}

// ============================================================================
/// @brief Write all finished object traces to disk.
void WriteFinishedObjectTraces() {
  RemoveBlacklistedMethods();

  Tid tid = GetTid();

  ::PIN_GetLock(&finishedObjectTracesLock, tid);

  cout << "writing object traces from thread " << tid << "..." << endl;
  string objectTracesPathStr = objectTracesPath.Value().c_str();

  ofstream os(objectTracesPathStr.c_str(), ios_base::app);
  assert(os.is_open());

  for (const ObjectTrace trace : finishedObjectTraces) {
    assert(trace != nullptr);
    for (const ObjectTraceEntry& entry : *trace) {
      os << hex << entry.procedure;
      if (entry.isCall) {
        os << " 1";
      }
      os << endl;
    }
    os << endl;
    delete trace;
  }

  totalObjectTraces += finishedObjectTraces.size();

  finishedObjectTraces.clear();
  cout << "object traces written" << endl;

  ::PIN_ReleaseLock(&finishedObjectTracesLock);
}

// ============================================================================
/// @brief Retrieve method candidates from file. Retrieve ground truth methods
/// if they exist (for evaluation).
void ParsePregame() {
  ifstream methodCandidatesStream(methodCandidatesPath.Value());
  if (!methodCandidatesStream.is_open()) {
    cerr << "failed to open methodCandidatesPath "
         << methodCandidatesPath.Value() << endl;
    exit(EXIT_FAILURE);
  }

  ADDRINT methodCandidate{};
  while (methodCandidatesStream >> hex >> methodCandidate >> ws) {
    methodCandidateAddrs.insert(methodCandidate);
  }

  ifstream gtMethodsStream(gtMethodsPath.Value());
  ADDRINT gtMethod{};
  while (gtMethodsStream >> hex >> gtMethod >> ws) {
    gtMethodAddrs.insert(gtMethod);
  }

  cout << "Populated method candidates" << endl;

  deleteOperators.push_back(DELETE_OPERATOR);
  deleteOperators.push_back(DELETE_OPERATOR_SIZE);

  string objectTracesPathStr = objectTracesPath.Value().c_str();

  ofstream os(objectTracesPathStr.c_str(), ofstream::out | ofstream::trunc);
}

// ============================================================================
/// @brief End the object-trace associated with the given iterator (an iterator
/// in the activeObjectTraces map). The object-trace is either moved to the
/// finished object trace if it is unique, or otherwise it is deleted if a
/// duplicate object-trace already exists.
/// @note it must be an iterator that has a valid non-null object-trace.
/// @note The iterator is not removed from the activeObjectTraces. The user is
/// responsible for erasing the iterator.
void EndObjectTraceIt(decltype(activeObjectTraces)::iterator& it, bool potentiallyWriteTraces) {
  assert(GetTid() == activeObjectTracesLock._owner);

  ObjectTrace objectTrace = it->second;
  assert(objectTrace != nullptr);
  // Relinquish ownership of the ObjectTrace (mostly just a safety measure to
  // ensure if the user does not remove the iterator from activeObjectTraces
  // they at least won't have access to the ObjectTrace they requested to end).
  it->second = nullptr;

  if (objectTrace->empty()) {
    // Don't insert empty object trace
    delete objectTrace;
  } else {
    Tid tid = GetTid();

    ::PIN_GetLock(&finishedObjectTracesLock, tid);

    // Check to see if the iterator associated with the object trace being
    // finished is already in finishedObjectTraces.
    const auto& finishedObjectTracesIt =
        find_if(finishedObjectTraces.begin(),
                finishedObjectTraces.end(),
                [objectTrace](const ObjectTrace finishedObjectTrace) {
                  return *finishedObjectTrace == *objectTrace;
                });

    if (finishedObjectTracesIt == finishedObjectTraces.end()) {
      if (objectTrace->size() == 1) {
        ::PIN_ReleaseLock(&finishedObjectTracesLock);

        // This is invalid since each call must have a matching return in the
        // object-trace.
        cout << "attempting to add object-trace with size 1: "
             << objectTrace->at(0) << endl;
        blacklistedProcedures.insert(objectTrace->at(0).procedure);
        delete objectTrace;
      } else {
        finishedObjectTraces.push_back(objectTrace);

        if (finishedObjectTraces.size() > FINISHED_OBJECT_TRACES_MAX_SIZE && potentiallyWriteTraces) {
          ::PIN_ReleaseLock(&finishedObjectTracesLock);
          WriteFinishedObjectTraces();
        } else {
          ::PIN_ReleaseLock(&finishedObjectTracesLock);
        }
      }
    } else {
      ::PIN_ReleaseLock(&finishedObjectTracesLock);

      // Don't insert duplicate object trace
      delete objectTrace;
    }
  }
}

// ============================================================================
/// @brief Ends object trace with object pointer address of objPtr. If objPtr
/// doesn't exist (isn't in activeObjectTraces), does nothing.
void EndObjectTrace(ADDRINT objPtr, bool potentiallyWriteTraces) {
  assert(GetTid() == activeObjectTracesLock._owner);

  auto it = activeObjectTraces.find(objPtr);
  if (it != activeObjectTraces.end()) {
    EndObjectTraceIt(it, potentiallyWriteTraces);
    activeObjectTraces.erase(it);
  }
}

// ============================================================================
/// @brief Ends any object traces whose object pointers reside within the
/// given region, that is, within the set of values [regionStart, regionEnd).
/// It is required that regionStart <= regionEnd.
/// The global activeObjectTraces object will be modified by this function.
void EndObjectTracesInRegion(ADDRINT regionStart, ADDRINT regionEnd, bool potentiallyWriteTraces) {
  // lower_bound means that the argument is the lower bound and that the
  // returned iterator points to an element greater than or equal to the
  // argument. And, as you'd expect, upper_bound points /after/ the position
  // you really want, to make it easy to use in the end condition of a loop.

  assert(GetTid() == activeObjectTracesLock._owner);
  assert(regionStart <= regionEnd);

  auto firstTrace = activeObjectTraces.lower_bound(regionStart);
  auto it = firstTrace;
  for (; it != activeObjectTraces.end() && it->first < regionEnd; it++) {
    EndObjectTraceIt(it, potentiallyWriteTraces);
  }
  activeObjectTraces.erase(firstTrace, it);
}

// ============================================================================
//                          Malloc/Free Observers
// ============================================================================

// ============================================================================
/// @brief Call when malloc is called. Stores the size malloced.
/// @param size Size to be malloc'ed.
void MallocBeforeCallback(ADDRINT size) {
  Tid tid = GetTid();

  ::PIN_GetLock(&tidToMallocSizeLock, tid);
  tidToMallocSize[tid] = size;
  ::PIN_ReleaseLock(&tidToMallocSizeLock);
}

// ============================================================================
/// @brief Call when malloc returned from. Adds new heap allocated region.
/// @param regionStart Start of region that has been allocated.
void MallocAfterCallback(ADDRINT regionStart) {
#ifdef LOG_WARN
  if (heapAllocatedRegions.find(regionStart) != heapAllocatedRegions.end()) {
    LOG("WARNING: Malloc'ing a pointer that was already malloc'ed!"
        "Maybe too many malloc procedures specified?\n");
    // TODO: could debug even further by searching for if the pointer lies
    // within an allocated region.
  }
#endif

  Tid tid = GetTid();

  ::PIN_GetLock(&tidToMallocSizeLock, tid);
  assert(tidToMallocSize.count(tid) != 0);
  ADDRINT mallocSize = tidToMallocSize[tid];
  tidToMallocSize.erase(tid);
  ::PIN_ReleaseLock(&tidToMallocSizeLock);

#ifdef LOG_INFO
  stringstream ss;
  ss << "Malloc @ " << regionStart << " (" << dec << mallocSize << " bytes)"
     << endl;
  LOG(ss.str());
#endif

  ::PIN_GetLock(&heapAllocatedRegionsLock, GetTid());
  heapAllocatedRegions[regionStart] = regionStart + mallocSize;
  ::PIN_ReleaseLock(&heapAllocatedRegionsLock);
}

// ============================================================================
/// @brief Call when free called. Removes the heap allocated region specified by
/// freedRegionStart.
/// @param freedRegionStart Start of region to be freed.
void FreeBeforeCallback(ADDRINT freedRegionStart) {
#ifdef LOG_INFO
  stringstream ss;
  ss << "Free @ " << freedRegionStart << endl;
  LOG(ss.str());
#endif

  if (freedRegionStart == 0) {
    // This is in fact well-defined and legal behavior.
    return;
  }

  Tid tid = GetTid();

  ::PIN_GetLock(&heapAllocatedRegionsLock, tid);

  auto freedRegionIt = heapAllocatedRegions.find(freedRegionStart);
  ADDRINT freedRegionEnd = freedRegionIt->second;

  if (freedRegionIt == heapAllocatedRegions.end()) {
#ifdef LOG_WARN
    LOG("WARNING: Invalid pointer freed! Check the program-under-test.\n");
#endif
    ::PIN_ReleaseLock(&heapAllocatedRegionsLock);
  } else {
    heapAllocatedRegions.erase(freedRegionIt);
    ::PIN_ReleaseLock(&heapAllocatedRegionsLock);

    ::PIN_GetLock(&activeObjectTracesLock, tid);
    EndObjectTracesInRegion(freedRegionStart, freedRegionEnd, true);
    ::PIN_ReleaseLock(&activeObjectTracesLock);
  }
}

// ============================================================================
/// @brief Call when delete is called. Ends the object-trace associated with the
/// deleted object.
/// @param deletedObject Pointer to object being deleted.
void OnDeleteInstrumented(ADDRINT deletedObject) {
#ifdef LOG_INFO
  stringstream ss;
  ss << "Delete @ " << deletedObject << endl;
  LOG(ss.str());
#endif

  Tid tid = GetTid();

  ::PIN_GetLock(&heapAllocatedRegionsLock, tid);

  // Delete called, to verify delete is valid search for it in
  // heapAllocatedRegions
  auto freedRegionIt = heapAllocatedRegions.find(deletedObject);
  if (freedRegionIt == heapAllocatedRegions.end()) {
#ifdef LOG_WARN
    LOG("Attempting to delete ptr that is not in heap allocated region\n");
#endif
    ::PIN_ReleaseLock(&heapAllocatedRegionsLock);
  } else {
    heapAllocatedRegions.erase(freedRegionIt);

    ::PIN_ReleaseLock(&heapAllocatedRegionsLock);

    ::PIN_GetLock(&activeObjectTracesLock, tid);
    EndObjectTrace(deletedObject, true);
    ::PIN_ReleaseLock(&activeObjectTracesLock);
  }
}

// ============================================================================
//                          Stack Pointer Observers
// ============================================================================

// ============================================================================
// a stack pointer change is interesting if it moves the pointer to the lowest
// known address, or moves it up past an active object trace. Possible
// microoptimization: For `add` operations on the stack pointer, only check if
// it's increased past an active object pointer, and for `sub` operations, only
// check if it's decreased below the lowest known.
// TODO: somehow track the lowest active objPtr in the stack. Could be done by
// checking if an objptr is above the stack during method calls. That way we
// don't have to call EndObjectTracesInRegion so frequently.

PIN_LOCK lastStackPtrLock;
map<Tid, ADDRINT> threadIdToLastStackPtrMap;

/// @brief Called before stack increase happens.
/// @param stackPtr Stack pointer at point before stack increase happens.
void StackIncreaseBeforeCallback(ADDRINT stackPtr) {
  Tid tid = GetTid();

  ::PIN_GetLock(&lastStackPtrLock, tid);
  threadIdToLastStackPtrMap[tid] = stackPtr;
  ::PIN_ReleaseLock(&lastStackPtrLock);
}

// ============================================================================
/// @return 1 if stack increase detected, 0 otherwise.
ADDRINT StackIncreasePredicate(ADDRINT stackPtr) {
  Tid tid = GetTid();
  ::PIN_GetLock(&lastStackPtrLock, tid);
  assert(threadIdToLastStackPtrMap.count(tid) != 0);
  ADDRINT ret = static_cast<ADDRINT>(stackPtr > threadIdToLastStackPtrMap[tid]);
  ::PIN_ReleaseLock(&lastStackPtrLock);
  return ret;
}

// ============================================================================
/// @brief Call if stack increase detected (implying some memory is being
/// deallocated). Ends object traces within location of stack decrease.
/// @param stackPtr New stack pointer.
void StackIncreaseCallback(ADDRINT stackPtr) {
  Tid tid = GetTid();

  // Update stack base if the stack pointer is < the currently recorded base.
  ::PIN_GetLock(&stackBaseLock, tid);
  if (tidToStackBaseMap.count(tid) == 0 || tidToStackBaseMap[tid] < stackPtr) {
    tidToStackBaseMap[tid] = stackPtr;
  }
  ::PIN_ReleaseLock(&stackBaseLock);

  ::PIN_GetLock(&lastStackPtrLock, tid);
  ADDRINT lastStackPtr = threadIdToLastStackPtrMap[tid];
  ::PIN_ReleaseLock(&lastStackPtrLock);

  // End traces in the deallocated region.
  ::PIN_GetLock(&activeObjectTracesLock, tid);
  EndObjectTracesInRegion(lastStackPtr, stackPtr, true);
  ::PIN_ReleaseLock(&activeObjectTracesLock);
}

// ============================================================================
/// @brief Checks to see if the given instruction is a stack increase. Will
/// return true if the instruction is possibly a stack increase (this
/// function is conservative, if it is not sure it will return `true` so
/// additional stack pointer observers can verify that the stack is in fact
/// increasing).
bool IsPossibleStackIncrease(INS ins) {
  OPCODE insOpcode = LEVEL_CORE::INS_Opcode(ins);

  // There /is/ one instruction which modifies the stack pointer which we
  // intentionally exclude here -- `ret`. While it moves the stack pointer up by
  // one, it shouldn't matter unless you are jumping to an object pointer -- and
  // in that case, we have larger problems!

  // Also note: It is important that we exclude ret and call instructions
  // because of how the InstrumentInstruction function uses this function.
  if (LEVEL_CORE::INS_IsRet(ins) || LEVEL_CORE::INS_IsSub(ins) ||
      LEVEL_CORE::INS_IsCall(ins)) {
    return false;
  }

  return INS_RegWContain(ins, REG_STACK_PTR);
}

// ============================================================================

/// @brief Called when a call instruction is called. Saves return address.
/// @param retAddr Expected return address for the given call.
void CallCallback(ADDRINT retAddr) {
  Tid tid = GetTid();
  ::PIN_GetLock(&threadToLastRetAddrLock, tid);
  threadToLastRetAddrMap[tid] = retAddr;
  ::PIN_ReleaseLock(&threadToLastRetAddrLock);
}

// ============================================================================
/// @brief Returns true if the ptr is within a region in the regionMap.
/// The region map maps starts of regions to (one past) the end of a
/// region.
bool WithinRegion(ADDRINT ptr, map<ADDRINT, ADDRINT> regionMap) {
  auto regIt = regionMap.upper_bound(ptr);
  if (regIt != regionMap.begin()) {
    regIt--;
    if (ptr <= regIt->second) {
      return true;
    }
  }

  return false;
}

// ============================================================================
/// @brief determine if an ADDRINT is a plausible object pointer.
bool IsPossibleObjPtr(ADDRINT ptr, ADDRINT stackPtr) {
  Tid tid = GetTid();

  ::PIN_GetLock(&stackBaseLock, tid);
  assert(tidToStackBaseMap.count(tid) != 0);
  ADDRINT stackBase = tidToStackBaseMap[tid];
  ::PIN_ReleaseLock(&stackBaseLock);

  // If within stack, address valid.
  if (ptr >= stackPtr && ptr <= stackBase) {
    return true;
  }

  // If lies within reg allocated region, address valid.
  if (WithinRegion(ptr, mappedRegions)) {
    return true;
  }

  ::PIN_GetLock(&heapAllocatedRegionsLock, tid);
  bool inHeapRegion = WithinRegion(ptr, heapAllocatedRegions);
  ::PIN_ReleaseLock(&heapAllocatedRegionsLock);

  return inHeapRegion;
}

// ============================================================================
/// @brief Inserts object trace entry into the active object traces.
/// @note Caller is required to obtain the activeObjectTracesLock prior to calling
/// This function.
void InsertObjectTraceEntry(ADDRINT objPtr, const ObjectTraceEntry& entry) {
  assert(GetTid() == activeObjectTracesLock._owner);

  auto objectTraceIt = activeObjectTraces.find(objPtr);

  // No active object-trace for the given objPtr, so create a new object trace.
  if (objectTraceIt == activeObjectTraces.end()) {
    // Since pin doesn't support c++11 on windows we have to manage heap
    // allocations manually :(
    activeObjectTraces[objPtr] = new vector<ObjectTraceEntry>();
    objectTraceIt = activeObjectTraces.find(objPtr);
  }

#ifdef LOG_INFO
  stringstream ss;
  ss << (entry.isCall ? "C" : "R") << " objPtr " << objPtr << " procAddr "
     << entry.procedure << endl;
  LOG(ss.str());
#endif

  objectTraceIt->second->push_back(entry);
}

// ============================================================================
// Shadow stack helpers
// ============================================================================s
/// @brief Removes the top entry in the shadow stack and decrements the
/// associated counter in the stackEntryCount. Also removes the entry in the
/// stackEntryCount if the counter is 0 to preserve memory.
/// @note The shadow stack must not be empty when this function is called.
ShadowStackEntry ShadowStackRemoveAndReturnTop() {
  Tid tid = GetTid();

  auto shadowStackIt = threadToShadowStackMap.find(tid);

  assert(shadowStackIt != threadToShadowStackMap.end() &&
         shadowStackIt->second.size() > 0);

  vector<ShadowStackEntry> &shadowStack = shadowStackIt->second;

  ShadowStackEntry stackTop = shadowStack.back();

  // Remove from shadow stack
  shadowStack.pop_back();

  // Remove from stackEntryCount
  auto stackEntryIt = stackEntryCount.find(stackTop.returnAddr);
  // This implies an inconsistency between the shadowStack and the
  // stackEntryCount.
  assert(stackEntryIt != stackEntryCount.end());
  stackEntryIt->second--;
  if (stackEntryIt->second == 0) {
    stackEntryCount.erase(stackEntryIt);
  }

  return stackTop;
}

// ============================================================================
/// @brief Handle calls with unmatched returns. Performs call-return matching.
/// Removes unmatched shadow stack frames as appropriate.
/// @note If shadow stack empty, return ignored.
/// @param actualRetAddr Actual (observed) return address.
bool IgnoreReturn(ADDRINT actualRetAddr) {
  Tid tid = GetTid();

  auto shadowStackIt = threadToShadowStackMap.find(tid);

  if (shadowStackIt == threadToShadowStackMap.end() ||
      shadowStackIt->second.empty()) {
    return true;
  }

  ShadowStackEntry stackTop = shadowStackIt->second.back();

  // Return address matches top of stack
  if (actualRetAddr == stackTop.returnAddr) {
    return false;
  }

  // The return address does not match the procedure on the top of the stack,
  // so we have to find the procedure on the stack that has a matching return
  // address.

  // Matching call in stack
  if (stackEntryCount.find(actualRetAddr) != stackEntryCount.end()) {
    // Pop unmatched frames
    while (actualRetAddr != stackTop.returnAddr) {
      ShadowStackRemoveAndReturnTop();
      stackTop = shadowStackIt->second.back();
    }

    assert(actualRetAddr == stackTop.returnAddr);
    return false;
  } else {
    return true;
  }
}

// ============================================================================
/// @brief Called when a procedure is instrumented (a methodCandidates address
/// is being instrumented).
/// @param procAddr Address of procedure being instrumented. This is a relative
/// address, normalized by subtracting the base address.
/// @param objPtr Address of object (this) pointer passed to the procedure. This
/// pointer is not normalized.
void MethodCandidateCallback(ADDRINT procAddr, ADDRINT stackPtr,
                             ADDRINT objPtr) {
  // Method blacklisted, don't add to trace
  if (blacklistedProcedures.count(procAddr) != 0) {
    return;
  }

  // Object pointer invalid, not possible method candidate
  if (!IsPossibleObjPtr(objPtr, stackPtr)) {
    blacklistedProcedures.insert(procAddr);
    return;
  }

  // The method candidate is valid. Add to the trace and shadow stack.
  // if (shadowStack.size() != 0) {
  //   ShadowStackEntry& trace_entry = shadowStack.back();
  //   // TODO insert procedure to calledProcedures
  // }

  Tid tid = GetTid();

  ::PIN_GetLock(&activeObjectTracesLock, tid);
  InsertObjectTraceEntry(objPtr, ObjectTraceEntry(procAddr, true));
  ::PIN_ReleaseLock(&activeObjectTracesLock);

  ::PIN_GetLock(&threadToLastRetAddrLock, tid);
  // should always appear just after a `call` (or should it?)
  if (threadToLastRetAddrMap.count(tid) == 0) {
    // return address invalid, don't add procedure to shadow stack
    // Potentially assert in the future (unclear is this should not
    // happen or if some weird application could cause this
    // conditional to be entered).

#ifdef LOG_WARN
    stringstream ss;
    ss << "Method executed without call! Likely not a method. procAddr "
       << procAddr << endl;
    LOG(ss.str());
#endif
  } else {
    ::PIN_GetLock(&shadowStackLock, tid);

    if (threadToShadowStackMap.count(tid) == 0) {
      threadToShadowStackMap[tid] = vector<ShadowStackEntry>();
    }
    
    threadToShadowStackMap[tid].push_back(
        ShadowStackEntry(objPtr, threadToLastRetAddrMap[tid], procAddr));

    ::PIN_ReleaseLock(&shadowStackLock);

    stackEntryCount[threadToLastRetAddrMap[tid]]++;

    threadToLastRetAddrMap.erase(tid);
  }

  ::PIN_ReleaseLock(&threadToLastRetAddrLock);
}

// ============================================================================
/// @brief Called when a procedure is returned from. Handles popping stack entry
/// from the shadow stack and inserting an entry in the correct object trace.
/// @param returnAddr Address being returned to (not normalized).
void RetCallback(ADDRINT returnAddr) {
  // TODO: maybe do this as an InsertIfCall? Probably won't help though because
  // map lookup probably can't be inlined.
  // TODO: perhaps include a compile time flag to use a simpler implementation
  // where we assume exact correspondence between `call` and `ret` to speed
  // things up on more predictable software

  // after some godbolt experimentation: GCC is capable of inlining hashtable
  // access, but the hash table access function has branches. if the ret is not
  // in the stack, then maybe there was some manual push-jump stuff going on, so
  // we ignore it.

  Tid tid = GetTid();

  ::PIN_GetLock(&shadowStackLock, tid);

  if (!IgnoreReturn(returnAddr)) {
    ShadowStackEntry stackTop = ShadowStackRemoveAndReturnTop();
    ::PIN_ReleaseLock(&shadowStackLock);

    ::PIN_GetLock(&activeObjectTracesLock, GetTid());

    // If the first element in the object trace is a returned value, the
    // function is likely a scalar/vector deleting destructor that is not
    // actually a method of the class. Blacklist the method.
    if (activeObjectTraces.find(stackTop.objPtr) == activeObjectTraces.end()) {
      blacklistedProcedures.insert(stackTop.procedure);
    } else {
      InsertObjectTraceEntry(stackTop.objPtr,
                             ObjectTraceEntry(stackTop.procedure, false));
    }

    ::PIN_ReleaseLock(&activeObjectTracesLock);
  } else {
    ::PIN_ReleaseLock(&shadowStackLock);
  }
}

// ============================================================================
//                              INSTRUMENTERS
// ============================================================================

#ifdef _WIN32
/// in this case, FUNCARG ENTRYPOINT stuff doesn't know whether it's a method or
/// not, and probably looks at the stack. TODO: figure out how FUNCARG
/// ENTRYPOINT works in general, and if we're allowed to put it on things we
/// haven't marked as routines.
#define IARG_KREO_FIRST_ARG IARG_REG_VALUE
#define IARG_KREO_FIRST_ARG_VALUE REG_ECX
#else
#define IARG_KREO_FIRST_ARG IARG_FUNCARG_ENTRYPOINT_VALUE
#define IARG_KREO_FIRST_ARG_VALUE 0
#endif

/// @brief Called during the instrumentation of each instruction. Main logic for
/// creating object traces done here. Thread safety satisfied because Pin holds
/// a client lock whenever an instruction is instrumented.
/// @param ins Instruction being instrumented.
void InstrumentInstruction(INS ins, void*) {
  // We want to instrument:
  // + Calls to candidate methods, to add to the trace.
  // + Return instructions, to add to the trace.
  // + Upward motion of the stack pointer, to track allocated memory and end
  //   traces in deallocated regions.

  ADDRINT insRelAddr = INS_Address(ins) - lowAddr;
  if (methodCandidateAddrs.count(insRelAddr) == 1) {
    INS_InsertCall(ins,
                   IPOINT_BEFORE,
                   reinterpret_cast<AFUNPTR>(MethodCandidateCallback),
                   IARG_ADDRINT,
                   insRelAddr,
                   IARG_REG_VALUE,
                   REG_STACK_PTR,
                   IARG_KREO_FIRST_ARG,
                   IARG_KREO_FIRST_ARG_VALUE,
                   IARG_END);
  }

  // If a procedure is being called that belongs to the ground truth, add it
  // to the gtMethodsInstrumented set. Note that this is only for evaluation
  // purposes.
  if (gtMethodAddrs.count(insRelAddr) == 1) {
    gtMethodsInstrumented.insert(insRelAddr);
  }

  bool insHandled = false;

  // The following types of instructions should be mutually exclusive.
  if (IsPossibleStackIncrease(ins)) {
    IPOINT where = INS_HasFallThrough(ins) ? IPOINT_AFTER : IPOINT_TAKEN_BRANCH;

    INS_InsertCall(ins,
                   IPOINT_BEFORE,
                   reinterpret_cast<AFUNPTR>(StackIncreaseBeforeCallback),
                   IARG_REG_VALUE,
                   REG_STACK_PTR,
                   IARG_END);
    INS_InsertIfCall(ins,
                     where,
                     reinterpret_cast<AFUNPTR>(StackIncreasePredicate),
                     IARG_REG_VALUE,
                     REG_STACK_PTR,
                     IARG_END);
    INS_InsertThenCall(ins,
                       where,
                       reinterpret_cast<AFUNPTR>(StackIncreaseCallback),
                       IARG_REG_VALUE,
                       REG_STACK_PTR,
                       IARG_END);

    insHandled = true;
  }
  
  if (INS_IsRet(ins)) {  // TODO: do we need to handle farret?
    assert(!insHandled);
    insHandled = true;
  
    // docs say `ret` implies control flow, but just want to be sure, since this
    // a precondition for IARG_BRANCH_TARGET_ADDR but idk if that does an
    // internal assert.
    assert(INS_IsControlFlow(ins));
    INS_InsertCall(ins,
                   ::IPOINT_BEFORE,
                   reinterpret_cast<AFUNPTR>(RetCallback),
                   ::IARG_BRANCH_TARGET_ADDR,
                   ::IARG_END);
  }

  if (INS_IsCall(ins)) {
    assert(!insHandled);
    insHandled = true;

    INS_InsertCall(ins,
                   IPOINT_BEFORE,
                   reinterpret_cast<AFUNPTR>(CallCallback),
                   IARG_ADDRINT,
                   INS_NextAddress(ins),
                   IARG_END);
  }
}

/// @brief Called when image loaded. Finds potential malloc and free procedures
/// and instruments them properly.
void InstrumentImage(IMG img, void*) {
  // This is just for debugging, curious if/why there would be multiple regions
  // assert(IMG_NumRegions(img) == 1);

  // track mapped memory regions
  for (UINT32 ii = 0; ii < IMG_NumRegions(img); ii++) {
    cout << "Loading region " << ii << " from 0x" << hex
         << IMG_RegionLowAddress(img, ii) << " through 0x"
         << IMG_RegionHighAddress(img, ii) << dec << endl;

    mappedRegions[IMG_RegionLowAddress(img, ii)] =
        IMG_RegionHighAddress(img, ii);
  }

  cout << "Loading image: " << IMG_Name(img)
       << ", regions: " << IMG_NumRegions(img) << endl;

  if (IMG_IsMainExecutable(img)) {  // TODO: change to target DLLs on demand
    lowAddr = IMG_LowAddress(img);

    // store all procedures with symbols into the global map for debugging use.
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
      for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
        procedureSymbolNames[RTN_Address(rtn) - lowAddr] = RTN_Name(rtn);
      }
    }

    for (std::string operatorName : deleteOperators) {
      RTN discoveredDelete = RTN_FindByName(img, operatorName.c_str());
      if (RTN_Valid(discoveredDelete)) {
        blacklistedProcedures.insert(RTN_Address(discoveredDelete) - lowAddr);

        RTN_Open(discoveredDelete);
        RTN_InsertCall(discoveredDelete,
                       IPOINT_BEFORE,
                       reinterpret_cast<AFUNPTR>(OnDeleteInstrumented),
                       IARG_FUNCARG_ENTRYPOINT_VALUE,
                       0,
                       IARG_END);
        RTN_Close(discoveredDelete);
      }
    }
  }

  // Insert malloc/free discovered by name into routine list

  // TODO: look more into what the malloc symbol is named on different
  // platforms.
  RTN mallocRtn = RTN_FindByName(img, MALLOC_SYMBOL);

  if (RTN_Valid(mallocRtn)) {
#ifdef LOG_INFO
    LOG("Found malloc procedure by symbol in img " + IMG_Name(img) + "\n");
#endif
    RTN_Open(mallocRtn);
    RTN_InsertCall(mallocRtn,
                   IPOINT_BEFORE,
                   reinterpret_cast<AFUNPTR>(MallocBeforeCallback),
                   IARG_FUNCARG_ENTRYPOINT_VALUE,
                   0,
                   IARG_END);
    RTN_InsertCall(mallocRtn,
                   IPOINT_AFTER,
                   reinterpret_cast<AFUNPTR>(MallocAfterCallback),
                   IARG_FUNCRET_EXITPOINT_VALUE,
                   IARG_END);
    RTN_Close(mallocRtn);
  }

  // TODO: it seems that `free` in one library calls `free`
  // in another library (at least on gnu libc linux), because...
  RTN freeRtn = RTN_FindByName(img, FREE_SYMBOL);

  if (RTN_Valid(freeRtn)) {
#ifdef LOG_INFO
    LOG("Found free procedure by symbol in img " + IMG_Name(img) + "\n");
#endif
    RTN_Open(freeRtn);
    RTN_InsertCall(freeRtn,
                   IPOINT_BEFORE,
                   reinterpret_cast<AFUNPTR>(FreeBeforeCallback),
                   IARG_FUNCARG_ENTRYPOINT_VALUE,
                   0,
                   IARG_END);
    RTN_Close(freeRtn);
  }
}

/// @brief Caled when image unloaded. Remove any mapped regions.
void InstrumentUnloadImage(IMG img, void*) {
  // remove regions from mapped memory tracker
  for (UINT32 i = 0; i < IMG_NumRegions(img); i++) {
    mappedRegions.erase(IMG_RegionLowAddress(img, i));
  }
}

void Fini(INT32 code, void*) {
  cout << "Program run completed, ending " << activeObjectTraces.size() << " active object traces" << endl;

  ::PIN_GetLock(&activeObjectTracesLock, GetTid());

  // End all in-progress traces, then print all to disk.
  auto lastObjectTraceIt = activeObjectTraces.rbegin();
  if (lastObjectTraceIt != activeObjectTraces.rend()) {
    EndObjectTracesInRegion(0, lastObjectTraceIt->first, false);
  }

  ::PIN_ReleaseLock(&activeObjectTracesLock);

  cout << "Blacklisted methods removed. Writing object-traces to a file, need to write " << finishedObjectTraces.size() << " object-traces..." << endl;
  WriteFinishedObjectTraces();
  cout << "Wrote approximately " << totalObjectTraces << " object-traces" << endl;

  ofstream os;
  os.close();
  string objectTracesPathStr = objectTracesPath.Value().c_str();
  os.open(std::string(objectTracesPathStr + "-name-map").c_str());
  for (const auto& it : procedureSymbolNames) {
    if (methodCandidateAddrs.find(it.first) != methodCandidateAddrs.end()) {
      os << hex << it.first << " " << it.second << endl;
    }
  }

  string gtMethodsInstrumentedStr = gtMethodsInstrumentedPath.Value().c_str();
  os.close();
  os.open(gtMethodsInstrumentedStr.c_str());

  for (ADDRINT addr : gtMethodsInstrumented) {
    os << hex << addr << endl;
  }

  string blacklistedMethodsStr = blacklistedMethodsPath.Value().c_str();
  os.close();
  os.open(blacklistedMethodsStr.c_str());
  for (ADDRINT proc : blacklistedProcedures) {
    os << hex << proc << endl;
  }

  cout << "Done! Exiting normally." << endl;
}

void OnThreadStart(THREADID threadIndex, LEVEL_VM::CONTEXT* ctxt, INT32 flags,
                   VOID* v) {
  ADDRINT sp = PIN_GetContextReg(ctxt, ::REG_STACK_PTR);

  Tid tid = GetTid();

  ::PIN_GetLock(&stackBaseLock, tid);

  cout << "Thread started " << tid << ", " << threadIndex
       << ", stack pointer: 0x" << hex << sp << dec << endl;

  tidToStackBaseMap[tid] = sp;

  ::PIN_ReleaseLock(&stackBaseLock);
}

void OnThreadFini(THREADID threadIndex, const ::CONTEXT* ctxt, INT32 code,
                  VOID* v) {
  cout << "Thread finished " << GetTid() << ", " << threadIndex << endl;
}

int main(int argc, char** argv) {
  PIN_InitSymbols();  // this /is/ necessary for debug symbols, but somehow it
  // doesn't have an entry in the PIN documentation? (though
  // its name is referenced in a few places).
  ::PIN_Init(argc, argv);
  ParsePregame();

  ::PIN_InitLock(&activeObjectTracesLock);
  ::PIN_InitLock(&heapAllocatedRegionsLock);
  ::PIN_InitLock(&stackBaseLock);
  ::PIN_InitLock(&threadToLastRetAddrLock);
  ::PIN_InitLock(&finishedObjectTracesLock);
  ::PIN_InitLock(&tidToMallocSizeLock);
  ::PIN_InitLock(&shadowStackLock);

  IMG_AddInstrumentFunction(InstrumentImage, NULL);
  IMG_AddUnloadFunction(InstrumentUnloadImage, NULL);
  INS_AddInstrumentFunction(InstrumentInstruction, NULL);
  PIN_AddThreadStartFunction(OnThreadStart, NULL);
  PIN_AddThreadFiniFunction(OnThreadFini, NULL);
  PIN_AddFiniFunction(Fini, NULL);

  // TODO consider implementing if we run into memory problems
  // PIN_AddOutOfMemoryFunction

  // Start program, never returns
  PIN_StartProgram();
  return 0;
}
