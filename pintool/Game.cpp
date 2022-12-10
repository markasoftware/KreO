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

// Uncomment these to log messages of various levels
// #define LOG_INFO
// #define LOG_WARN
// #define LOG_ERROR

using namespace std;

// HACK: on Windows, Pintool uses STLP, which puts lots of the C++11 standard
// library data structures into the tr1 namespace (as was normal pre-C++11).
// Of course, this is fairly fragile because it depends on the exact
// implementation Pin uses...but I doubt they'll change it in any way other
// than eventually removing it and supporting C++11 on Windows
#ifdef _STLP_BEGIN_TR1_NAMESPACE
using namespace std::tr1;
#endif

// malloc names -- not sure how correct it is, but it's how it's done in
// malloctrace.c from pin examples
#ifdef TARGET_MAC
#define MALLOC_SYMBOL "_malloc"
#define FREE_SYMBOL "_free"
#else
#define MALLOC_SYMBOL "malloc"
#define FREE_SYMBOL "free"
#endif

// Mangled delete operator (msvc-specific), not sure how applicable this is to
// other compilers
#define DELETE_OPERATOR_SIZE "??3@YAXPAXI@Z"
#define DELETE_OPERATOR "??3@YAXPAX@Z"
vector<string> deleteOperators;

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

  // set of directly called procedures TODO currently creating unordered_sets
  // for each ObjectTraceEntry causes the application to run out of memory

  friend bool operator==(const ObjectTraceEntry& e1,
                         const ObjectTraceEntry& e2) {
    return e1.procedure == e2.procedure && e1.isCall == e2.isCall;
  }

  friend ostream& operator<<(ostream& os, const ObjectTraceEntry& o) {
    os << "{" << o.procedure << ", " << o.isCall << "}";
    return os;
  }
};

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

// TODO: make these knobs:
vector<ADDRINT> mallocProcedures;
vector<ADDRINT> freeProcedures;

// Potential method candidates, populated from the method candidates found
// during pregame
unordered_set<ADDRINT> methodCandidateAddrs;
unordered_set<ADDRINT> gtCalledMethods;
void GtMethodCallback(ADDRINT methodAddr) {
  assert(methodCandidateAddrs.count(methodAddr) == 1);
  gtCalledMethods.insert(methodAddr);
}

// Ground truth method candidates, populated from ground truth method
// candidates. Only used during evaluation and not used by KreO itself, just
// used to measure method coverage.
unordered_set<ADDRINT> gtMethodAddrs;

// TODO: potential optimization: Don't store finishedObjectTraces separately
// from activeObjectTraces; instead just have a single map<ADDRINT,
// vector<TraceEntry>>, and then insert special trace objects indicating when
// memory has been deallocated, allowing to easily split them at the end.
// Problem then is that multiple "splits" may be inserted, so theoretically an
// old trace whose memory keeps getting allocated and deallocated could be
// problematic...but does that even happen?
vector<vector<ObjectTraceEntry>*> finishedObjectTraces;
map<ADDRINT, vector<ObjectTraceEntry>*> activeObjectTraces;

vector<ShadowStackEntry> shadowStack;

// number of times each return address appears in the method stack.
unordered_map<ADDRINT, int> stackEntryCount;

// map starts of regions to (one past the) ends of regions.
map<ADDRINT, ADDRINT> heapAllocations;

// ^^^, but for mapped areas of memory (images). Used to determine valid object
// ptrs
map<ADDRINT, ADDRINT> mappedRegions;

// base offset of the executable image
ADDRINT lowAddr;

// Stack base
// TODO this might not work for multithreaded applications
ADDRINT stackBase{};

// Set of blacklisted addresses. Any address in the blacklist will have any
// methods that call the address as this receiver removed from the list of
// finalized object traces.
set<ADDRINT> blacklistedProcedures;

// can't use RTN methods during Fini, so we explicitly store all the symbol
// names here (for debugging)
unordered_map<ADDRINT, string> procedureSymbolNames;

PIN_LOCK checkObjectTraceLock;

// ============================================================================
/// @brief Removes any methods that are blacklisted from the finished object
/// traces. Also removes the trace if the trace is empty after blacklisted
/// procedures are removed from it.
void RemoveBlacklistedMethods() {
  for (auto traceIt = finishedObjectTraces.begin();
       traceIt != finishedObjectTraces.end();) {
    vector<ObjectTraceEntry>* trace = *traceIt;
    assert(trace != nullptr);

    for (auto entryIt = trace->begin(); entryIt != trace->end();) {
      if (blacklistedProcedures.count(entryIt->procedure)) {
        entryIt = trace->erase(entryIt);
      } else {
        entryIt++;
      }
    }

    if (trace->empty()) {
      traceIt = finishedObjectTraces.erase(traceIt);
      delete trace;
    } else {
      traceIt++;
    }
  }
}

// ============================================================================
/// @brief Writes finished object traces to disk.
void WriteObjectTraces() {
  RemoveBlacklistedMethods();

  cout << "writing object traces..." << endl;
  string objectTracesPathStr = objectTracesPath.Value().c_str();

  ofstream os(objectTracesPathStr.c_str(), ios_base::app);
  assert(os.is_open());

  for (const vector<ObjectTraceEntry>* trace : finishedObjectTraces) {
    assert(trace != nullptr);
    for (const ObjectTraceEntry& entry : *trace) {
      os << entry.procedure << " " << entry.isCall << endl;
    }
    os << endl;
    delete trace;
  }

  finishedObjectTraces.clear();
  cout << "object traces written" << endl;
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
  while (methodCandidatesStream >> methodCandidate >> ws) {
    methodCandidateAddrs.insert(methodCandidate);
  }

  ifstream gtMethodsStream(gtMethodsPath.Value());
  ADDRINT gtMethod{};
  while (gtMethodsStream >> gtMethod >> ws) {
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
void EndObjectTraceIt(decltype(activeObjectTraces)::iterator& it) {
  std::vector<ObjectTraceEntry>* objectTrace = it->second;
  assert(objectTrace != nullptr);
  it->second = nullptr;

  if (objectTrace->empty()) {
    // Don't insert empty object trace
    delete objectTrace;
  } else {
    const auto& finishedObjectTracesIt = find_if(
        finishedObjectTraces.begin(),
        finishedObjectTraces.end(),
        [objectTrace](const vector<ObjectTraceEntry>* finishedObjectTrace) {
          return *finishedObjectTrace == *objectTrace;
        });
    if (finishedObjectTracesIt == finishedObjectTraces.end()) {
      if (objectTrace->size() == 1) {
        // This is invalid since each call must have a matching return in the
        // object-trace.
        cerr << "attempting to add object-trace with size 1: "
             << objectTrace->at(0) << endl;
        blacklistedProcedures.insert(objectTrace->at(0).procedure);
        delete objectTrace;
      } else {
        finishedObjectTraces.push_back(objectTrace);

        if (finishedObjectTraces.size() > 1000) {
          WriteObjectTraces();
          cout << "###### " << dec << activeObjectTraces.size() << endl;
        }
      }
    } else {
      // Don't insert duplicate object trace
      delete objectTrace;
    }
  }
}

// ============================================================================
/// @brief Ends object trace with object pointer address of objPtr. If objPtr
/// doesn't exist (isn't in activeObjectTraces), does nothing.
void EndObjectTrace(ADDRINT objPtr) {
  auto it = activeObjectTraces.find(objPtr);
  if (it != activeObjectTraces.end()) {
    EndObjectTraceIt(it);
    activeObjectTraces.erase(it);
  }
}

// ============================================================================
/// @brief Ends any object traces whose object pointers reside within the
/// given region, that is, within the set of values [regionStart, regionEnd).
void EndObjectTracesInRegion(ADDRINT regionStart, ADDRINT regionEnd) {
  // lower_bound means that the argument is the lower bound and that the
  // returned iterator points to an element greater than or equal to the
  // argument. And, as you'd expect, upper_bound points /after/ the position
  // you really want, to make it easy to use in the end condition of a loop..

  auto firstTrace = activeObjectTraces.lower_bound(regionStart);
  auto it = firstTrace;
  for (;
       it != activeObjectTraces.end() && it->first < regionEnd;
       it++) {
    EndObjectTraceIt(it);
    assert(it->second == nullptr);
  }
  activeObjectTraces.erase(firstTrace, it);
}

///////////////////////
// ANALYSIS ROUTINES //
///////////////////////

// ============================================================================
//                          Malloc/Free Observers
// ============================================================================

ADDRINT mallocSize{};  // saves arg passed to most recent malloc invocation

// ============================================================================
// TODO this won't work when an executable being instrumented has threads
/// @brief Call when malloc is called. Stores the size malloced.
/// @param size Size to be malloc'ed.
void MallocBeforeCallback(ADDRINT size) { mallocSize = size; }

// ============================================================================
/// @brief Call when malloc returned from. Adds new heap allocated region.
/// @param regionStart Start of region that has been allocated.
void MallocAfterCallback(ADDRINT regionStart) {
  PIN_GetLock(&checkObjectTraceLock, 0);

#ifdef LOG_WARN
  if (heapAllocations.find(regionStart) != heapAllocations.end()) {
    LOG("WARNING: Malloc'ing a pointer that was already malloc'ed!"
        "Maybe too many malloc procedures specified?\n");
    // TODO: could debug even further by searching for if the pointer lies
    // within an allocated region.
  }
#endif

#ifdef LOG_INFO
  stringstream ss;
  ss << "Malloc @ " << regionStart << " (" << dec << mallocSize << " bytes)"
     << endl;
  LOG(ss.str());
#endif

  heapAllocations[regionStart] = regionStart + mallocSize;

  PIN_ReleaseLock(&checkObjectTraceLock);
}

// ============================================================================
/// @brief Call when free called. Removes the heap allocated region specified by
/// freedRegionStart.
/// @param freedRegionStart Start of region to be freed.
void FreeCallback(ADDRINT freedRegionStart) {
#ifdef LOG_INFO
  stringstream ss;
  ss << "Free @ " << freedRegionStart << endl;
  LOG(ss.str());
#endif

  if (freedRegionStart == 0) {
    // this is in fact well-defined and legal behavior
    return;
  }

  PIN_GetLock(&checkObjectTraceLock, 0);

  auto freedRegionIt = heapAllocations.find(freedRegionStart);
  ADDRINT freedRegionEnd = freedRegionIt->second;

  if (freedRegionIt == heapAllocations.end()) {
#ifdef LOG_WARN
    LOG("WARNING: Invalid pointer freed! Check the program-under-test.\n");
#endif
    goto cleanup;
  }

  EndObjectTracesInRegion(freedRegionStart, freedRegionEnd);
  heapAllocations.erase(freedRegionIt);

cleanup:
  PIN_ReleaseLock(&checkObjectTraceLock);
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

  PIN_GetLock(&checkObjectTraceLock, 0);

  // Delete called, to verify delete is valid search for it in heapAllocations
  auto freedRegionIt = heapAllocations.find(deletedObject);
  if (freedRegionIt == heapAllocations.end()) {
#ifdef LOG_WARN
    LOG("Attempting to delete ptr that is not in heap allocated region\n");
#endif
    goto cleanup;
  }

  EndObjectTrace(deletedObject);
  heapAllocations.erase(freedRegionIt);

cleanup:
  PIN_ReleaseLock(&checkObjectTraceLock);
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
ADDRINT lastStackPtr{};
/// @brief Call before stack incrase happens.
/// @param stackPtr Stack pointer at point before stack increase happens.
void StackIncreaseBeforeCallback(ADDRINT stackPtr) { lastStackPtr = stackPtr; }

// ============================================================================
/// @return True if stack increase detected, false otherwise.
bool StackIncreasePredicate(ADDRINT stackPtr) {
  return stackPtr > lastStackPtr;
}

// ============================================================================
/// @brief Call if stack increase detected. Ends object traces within location
/// of stack decrease.
/// @param stackPtr New stack pointer.
void StackIncreaseCallback(ADDRINT stackPtr) {
  PIN_GetLock(&checkObjectTraceLock, 0);
  EndObjectTracesInRegion(lastStackPtr, stackPtr);
  PIN_ReleaseLock(&checkObjectTraceLock);
}

// ============================================================================
/// @brief Called when a call instruction is called. Saves return address.
/// @param retAddr Expected return address for the given call.
ADDRINT lastRetAddr = -1;
void CallCallback(ADDRINT retAddr) { lastRetAddr = retAddr; }

// ============================================================================
/// @brief determine if an ADDRINT is a plausible object pointer.
bool IsPossibleObjPtr(ADDRINT ptr, ADDRINT stackPtr) {
  // TODO Checking if it's in an allocated region is not good enough because the
  // regions can change without triggering an image load, eg with brk syscall.

  // If within stack, address valid
  if (ptr >= stackPtr && ptr <= stackBase) {
    return true;
  }

  // If lies within reg allocated region, address valid.
  auto regIt = mappedRegions.upper_bound(ptr);
  if (regIt != mappedRegions.begin()) {
    regIt--;
    if (ptr < regIt->second) {
      return true;
    }
  }

  // If lies within heap allocated region, address valid.
  auto heapIt = heapAllocations.upper_bound(ptr);
  if (heapIt != heapAllocations.begin()) {
    heapIt--;
    if (ptr < heapIt->second) {
      return true;
    }
  }

  return false;
}

// ============================================================================
/// @brief Inserts object trace entry into the active object traces
void InsertObjectTraceEntry(ADDRINT objPtr, const ObjectTraceEntry& entry) {
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
// ============================================================================
/// @brief Removes the top entry in the shadow stack and decrements the
/// associated counter in the stackEntryCount. Also removes the entry in the
/// stackEntryCount if the counter is 0 to preserve memory.
/// @note The shadow stack must not be empty when this function is called.
ShadowStackEntry ShadowStackRemoveAndReturnTop() {
  assert(shadowStack.size() > 0);

  ShadowStackEntry stackTop = shadowStack.back();

  // Remove from shadow stack
  shadowStack.pop_back();

  // Remove from stackEntryCount
  auto stackEntryIt = stackEntryCount.find(stackTop.returnAddr);
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
  if (shadowStack.empty()) {
    return true;
  }

  ShadowStackEntry stackTop = shadowStack.back();

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
      stackTop = shadowStack.back();
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
  PIN_GetLock(&checkObjectTraceLock, 0);

  // Method blacklisted, don't add to trace
  if (blacklistedProcedures.count(procAddr) != 0) {
      goto cleanup;
  }

  // Object pointer invalid, not possible method candidate
  if (!IsPossibleObjPtr(objPtr, stackPtr)) {
    blacklistedProcedures.insert(procAddr);
    goto cleanup;
  }

  // The method candidate is valid. Add to the trace and shadow stack.
  // if (shadowStack.size() != 0) {
  //   ShadowStackEntry& trace_entry = shadowStack.back();
  //   // TODO insert procedure to calledProcedures
  // }

  InsertObjectTraceEntry(objPtr, ObjectTraceEntry(procAddr, true));

  // should always appear just after a `call` (or should it?)
  if (lastRetAddr == static_cast<ADDRINT>(-1)) {
    // return address invalid, don't add procedure to shadow stack
#ifdef LOG_WARN
    stringstream ss;
    ss << "Method executed without call! Likely not a method. procAddr "
       << procAddr << endl;
    LOG(ss.str());
#endif
  } else {
    shadowStack.push_back(ShadowStackEntry(objPtr, lastRetAddr, procAddr));
    stackEntryCount[lastRetAddr]++;
  }

  lastRetAddr = static_cast<ADDRINT>(-1);

cleanup:
  PIN_ReleaseLock(&checkObjectTraceLock);
}

// ============================================================================
/// @brief Called when a procedure is returned from. Handles popping stack entry
/// from the shadow stack and inserting an entry in the correct object trace.
/// @param returnAddr Address being returned to (not normalized).
void RetCallback(ADDRINT returnAddr) {
  PIN_GetLock(&checkObjectTraceLock, 0);

  // TODO: maybe do this as an InsertIfCall? Probably won't help though because
  // map lookup probably can't be inlined.
  // TODO: perhaps include a compile time flag to use a simpler implementation
  // where we assume exact correspondence between `call` and `ret` to speed
  // things up on more predictable software

  // after some godbolt experimentation: GCC is capable of inlining hashtable
  // access, but the hash table access function has branches. if the ret is not
  // in the stack, then maybe there was some manual push-jump stuff going on, so
  // we ignore it.

  if (!IgnoreReturn(returnAddr)) {
    ShadowStackEntry stackTop = ShadowStackRemoveAndReturnTop();

    // If the first element in the object trace is a returned value, the
    // function is likely a scalar/vector deleting destructor that is not
    // actually a method of the class. Blacklist the method.
    if (activeObjectTraces.find(stackTop.objPtr) == activeObjectTraces.end()) {
      blacklistedProcedures.insert(stackTop.procedure);
    } else {
      InsertObjectTraceEntry(stackTop.objPtr,
                             ObjectTraceEntry(stackTop.procedure, false));
    }
  }

  PIN_ReleaseLock(&checkObjectTraceLock);
}

void EntryPointCallback(ADDRINT rsp) {
    cerr << "Initial stack pointer: 0x" << hex << rsp << endl;
    stackBase = rsp;
}

bool IsPossibleStackIncrease(INS ins) {
  // TODO: check for other instructions we might want to exclude, or handle
  // specially (branches can't modify sp, right?)

  // There /is/ one instruction which modifies the stack pointer which we
  // intentionally exclude here -- `ret`. While it moves the stack pointer up by
  // one, it shouldn't matter unless you are jumping to an object pointer -- and
  // in that case, we have larger problems!

  if (INS_IsSub(ins)) {
    return false;
  }

  UINT32 operandCount = INS_OperandCount(ins);
  for (UINT32 opIdx = 0; opIdx < operandCount; opIdx++) {
    if (INS_OperandWritten(ins, opIdx) &&
        INS_OperandReg(ins, opIdx) == REG_STACK_PTR) {
      return true;
    }
  }
  return false;
}

// ============================================================================
//                              INSTRUMENTERS
// ============================================================================

#ifdef _WIN32
// in this case, FUNCARG ENTRYPOINT stuff doesn't know whether it's a method or
// not, and probably looks at the stack. TODO: figure out how FUNCARG ENTRYPOINT
// works in general, and if we're allowed to put it on things we haven't marked
// as routines.
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
  // + Upward motion of the stack pointer, to track allocated memory and perhaps
  // end traces.

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

  if (gtMethodAddrs.count(insRelAddr) == 1) {
    INS_InsertCall(ins,
                   IPOINT_BEFORE,
                   reinterpret_cast<AFUNPTR>(GtMethodCallback),
                   IARG_ADDRINT,
                   insRelAddr,
                   IARG_END);
  }

  // The following types of instructions should be mutually exclusive.
  bool alreadyInstrumented = false;
  if (IsPossibleStackIncrease(ins)) {
    assert(!alreadyInstrumented);
    alreadyInstrumented = true;

    INS_InsertCall(ins,
                   IPOINT_BEFORE,
                   reinterpret_cast<AFUNPTR>(StackIncreaseBeforeCallback),
                   IARG_REG_VALUE,
                   REG_STACK_PTR,
                   IARG_END);
    INS_InsertIfCall(ins,
                     IPOINT_AFTER,
                     reinterpret_cast<AFUNPTR>(StackIncreasePredicate),
                     IARG_REG_VALUE,
                     REG_STACK_PTR,
                     IARG_END);
    INS_InsertThenCall(ins,
                       IPOINT_AFTER,
                       reinterpret_cast<AFUNPTR>(StackIncreaseCallback),
                       IARG_REG_VALUE,
                       REG_STACK_PTR,
                       IARG_END);
  }

  if (INS_IsRet(ins)) {  // TODO: do we need to handle farret?
    assert(!alreadyInstrumented);
    alreadyInstrumented = true;

    // docs say `ret` implies control flow, but just want to be sure, since this
    // a precondition for IARG_BRANCH_TARGET_ADDR but idk if that does an
    // internal assert.
    assert(INS_IsControlFlow(ins));
    INS_InsertCall(ins,
                   IPOINT_BEFORE,
                   reinterpret_cast<AFUNPTR>(RetCallback),
                   IARG_BRANCH_TARGET_ADDR,
                   IARG_END);
  }

  if (INS_IsCall(ins)) {
    assert(!alreadyInstrumented);
    alreadyInstrumented = true;

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
  for (UINT32 i = 0; i < IMG_NumRegions(img); i++) {
      cout << "Region " << dec << i << " from 0x" << hex << IMG_RegionLowAddress(img, i)
           << " through 0x" << IMG_RegionHighAddress(img, i) << endl;
    mappedRegions[IMG_RegionLowAddress(img, i)] =
        IMG_RegionHighAddress(img, i) + 1;
  }

  cout << hex << IMG_Name(img) << ", " << dec << IMG_NumRegions(img) << hex << " many regions" << endl;

  // TODO: what if the user specifies a routine that's already detected
  // automatically? Want to make sure we don't add it twice, but RTN isn't
  // suitable for use in a set, so we need some manual way to prevent
  // duplicates! (just compare addresses?)
  vector<RTN> mallocRtns;
  vector<RTN> freeRtns;
  if (IMG_IsMainExecutable(img)) { // TODO: change to target DLLs on demand
    lowAddr = IMG_LowAddress(img);

    // store all procedures with symbols into the global map for debugging use.
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
      for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
        procedureSymbolNames[RTN_Address(rtn) - lowAddr] = RTN_Name(rtn);
      }
    }

    // HACK until we find the right way to determine the entry point
    RTN mainRtn = RTN_FindByName(img, "main");
    if (RTN_Valid(mainRtn)) {
        RTN_Open(mainRtn);
        RTN_InsertCall(mainRtn,
                       IPOINT_BEFORE,
                       reinterpret_cast<AFUNPTR>(EntryPointCallback),
                       IARG_REG_VALUE,
                       REG_STACK_PTR,
                       IARG_END);
        RTN_Close(mainRtn);
    }

    for (size_t i = 0; i < mallocProcedures.size(); i++) {
      // create routine if does not exist
      ADDRINT mallocAddr = mallocProcedures[i];
      if (!RTN_Valid(RTN_FindByAddress(mallocAddr + lowAddr))) {
        string name = "malloc_custom_" + to_string(i);
        RTN_CreateAt(mallocAddr + lowAddr, name);
      }
      mallocRtns.push_back(RTN_FindByAddress(mallocAddr + lowAddr));
    }
    for (size_t i = 0; i < freeProcedures.size(); i++) {
      // create routine if does not exist
      ADDRINT freeAddr = freeProcedures[i];
      if (!RTN_Valid(RTN_FindByAddress(freeAddr + lowAddr))) {
        string name = "free_custom_" + to_string(i);
        RTN_CreateAt(freeAddr + lowAddr, name);
      }
      freeRtns.push_back(RTN_FindByAddress(freeAddr + lowAddr));
    }
  }

  // TODO: look more into what the malloc symbol is named on different
  // platforms.
  RTN discoveredMalloc = RTN_FindByName(img, MALLOC_SYMBOL);

  // TODO: it seems that `free` in one library calls `free`
  // in another library (at least on gnu libc linux), because...
  RTN discoveredFree = RTN_FindByName(img, FREE_SYMBOL);

  // TODO test this on compilers other than MSVC in the future
  for (std::string operatorName : deleteOperators) {
    RTN discoveredDelete = RTN_FindByName(img, operatorName.c_str());
    if (RTN_Valid(discoveredDelete)) {
      cout << "discovered delete operator " << RTN_Address(discoveredDelete)
           << endl;
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

  // Insert malloc/free discovered by name into routine list

  if (RTN_Valid(discoveredMalloc)) {
#ifdef LOG_INFO
    LOG("Found malloc procedure by symbol in img " + IMG_Name(img) + "\n");
#endif
    mallocRtns.push_back(discoveredMalloc);
  } else {
#ifdef LOG_INFO
    LOG("Failed to find malloc procedure by symbol in img " + IMG_Name(img) +
        "\n");
#endif
  }

  if (RTN_Valid(discoveredFree)) {
#ifdef LOG_INFO
    LOG("Found free procedure by symbol in img " + IMG_Name(img) + "\n");
#endif
    freeRtns.push_back(discoveredFree);
  } else {
#ifdef LOG_INFO
    LOG("Failed to find free procedure by symbol in img " + IMG_Name(img) +
        "\n");
#endif
  }

  for (RTN mallocRtn : mallocRtns) {
    RTN_Open(mallocRtn);
    // save first argument
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

  for (RTN freeRtn : freeRtns) {
    RTN_Open(freeRtn);
    RTN_InsertCall(freeRtn,
                   IPOINT_BEFORE,
                   reinterpret_cast<AFUNPTR>(FreeCallback),
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
  cout << "Program run completed, writing object traces to disk..." << endl;

  // end all in-progress traces, then print all to disk.
  auto lastObjectTraceIt = activeObjectTraces.rbegin();
  if (lastObjectTraceIt != activeObjectTraces.rend()) {
    EndObjectTracesInRegion(0, lastObjectTraceIt->first);
  }

  cout << dec << "Blacklisted methods removed, found "
       << finishedObjectTraces.size()
       << " valid unique object traces. Writing object traces to a file..."
       << endl;

  WriteObjectTraces();

  ofstream os;
  os.close();
  string objectTracesPathStr = objectTracesPath.Value().c_str();
  os.open(std::string(objectTracesPathStr + "-name-map").c_str());
  for (const auto& it : procedureSymbolNames) {
    os << it.first << " " << it.second << endl;
  }

  string gtMethodsInstrumentedStr = gtMethodsInstrumentedPath.Value().c_str();
  os.close();
  os.open(gtMethodsInstrumentedStr.c_str());

  for (ADDRINT addr : gtCalledMethods) {
    os << addr << endl;
  }

  string blacklistedMethodsStr = blacklistedMethodsPath.Value().c_str();
  os.close();
  os.open(blacklistedMethodsStr.c_str());
  for (ADDRINT proc : blacklistedProcedures) {
    os << proc << endl;
  }

  cout << "Done! Exiting normally." << endl;
}

int main(int argc, char** argv) {
  ParsePregame();

  PIN_InitSymbols();  // this /is/ necessary for debug symbols, but somehow it
  // doesn't have an entry in the PIN documentation? (though
  // its name is referenced in a few places).
  PIN_Init(argc, argv);
  PIN_InitLock(&checkObjectTraceLock);
  IMG_AddInstrumentFunction(InstrumentImage, NULL);
  IMG_AddUnloadFunction(InstrumentUnloadImage, NULL);
  INS_AddInstrumentFunction(InstrumentInstruction, NULL);
  PIN_AddFiniFunction(Fini, NULL);

  // Start program, never returns
  PIN_StartProgram();
  return 0;
}
