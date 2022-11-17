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

// If you don't want to log any debugging information, uncomment these lines out
#undef LOG
#define LOG(x)

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

// custom structures

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
// TODO move method coverage to separate pintool independent of KreO.
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

// ============================================================================
/// @brief Retrieve method candidates from file
void ParsePregame() {
  ifstream methodCandidatesStream(methodCandidatesPath.Value());
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
}

// ============================================================================
/// @brief Ends any object traces whose object pointers reside within the given
/// region, that is, within the set of values [regionStart, regionEnd).
void EndObjectTracesInRegion(ADDRINT regionStart, ADDRINT regionEnd) {
  // lower_bound means that the argument is the lower bound and that the
  // returned iterator points to an element greater than or equal to the
  // argument. And, as you'd expect, upper_bound points /after/ the position you
  // really want, to make it easy to use in the end condition of a loop..

  auto firstTrace = activeObjectTraces.lower_bound(regionStart);
  auto lastTrace = activeObjectTraces.end();
  auto it = firstTrace;
  for (; it != lastTrace && it->first < regionEnd; it++) {
    if (it->second->empty()) {
      // Don't insert empty object trace
      delete it->second;
      it->second = nullptr;
    } else {
      const auto& finishedObjectTracesIt =
          find_if(finishedObjectTraces.begin(),
                  finishedObjectTraces.end(),
                  [it](const vector<ObjectTraceEntry>* objectTrace) {
                    return *objectTrace == *it->second;
                  });
      if (finishedObjectTracesIt == finishedObjectTraces.end()) {
        finishedObjectTraces.push_back(it->second);
      } else {
        // Don't insert duplicate object trace
        delete it->second;
        it->second = nullptr;
      }
    }
  }
  activeObjectTraces.erase(firstTrace, it);
}

///////////////////////
// ANALYSIS ROUTINES //
///////////////////////

// ============================================================================
//                          Malloc/Free Observers
// ============================================================================

ADDRINT mallocSize;  // saves arg passed to most recent malloc invocation

// TODO this won't work when an executable being instrumented has threads
void MallocBeforeCallback(ADDRINT size) { mallocSize = size; }

void MallocAfterCallback(ADDRINT regionStart) {
#ifndef SUPPRESS_MALLOC_ERRORS
  if (heapAllocations.find(regionStart) != heapAllocations.end()) {
    //   LOG("PINTOOL WARNING: Malloc'ing a pointer that was already malloc'ed!
    //   "
    //       "Maybe too many malloc procedures specified?\n");
    // TODO: could debug even further by searching for if the pointer lies
    // within an allocated region.
  }
#endif

  // stringstream ss;
  // ss << "Malloc @ " << hex << regionStart << " (" << dec << mallocSize
  //    << " bytes)" << endl;
  // LOG(ss.str());
  heapAllocations[regionStart] = regionStart + mallocSize;
}

void FreeCallback(ADDRINT freedRegionStart) {
  // stringstream ss;
  // ss << "Free @ " << hex << freedRegionStart << endl;
  // LOG(ss.str());

  if (freedRegionStart == 0) {
    // for whatever reason, real programs seem to be doing this??
    // LOG("WARNING: Freed nullptr?");
    return;
  }

  auto freedRegionIt = heapAllocations.find(freedRegionStart);

  if (freedRegionIt == heapAllocations.end()) {
#ifndef SUPPRESS_MALLOC_ERRORS
    // LOG("WARNING: Invalid pointer freed! Check the program-under-test.\n");
    return;
#endif
  }

  ADDRINT freedRegionEnd = freedRegionIt->second;
  EndObjectTracesInRegion(freedRegionStart, freedRegionEnd);
  heapAllocations.erase(freedRegionIt);
}

// ============================================================================
//                          Stack Pointer Observers
// ============================================================================

// a stack pointer change is interesting if it moves the pointer to the lowest
// known address, or moves it up past an active object trace. Possible
// microoptimization: For `add` operations on the stack pointer, only check if
// it's increased past an active object pointer, and for `sub` operations, only
// check if it's decreased below the lowest known.
// TODO: somehow track the lowest active objPtr in the stack. Could be done by
// checking if an objptr is above the stack during method calls. That way we
// don't have to call EndObjectTracesInRegion so frequently.
ADDRINT lastStackPtr;
void StackIncreaseBeforeCallback(ADDRINT stackPtr) { lastStackPtr = stackPtr; }
bool StackIncreasePredicate(ADDRINT stackPtr) {
  return stackPtr > lastStackPtr;
}
void StackIncreaseCallback(ADDRINT stackPtr) {
  EndObjectTracesInRegion(lastStackPtr, stackPtr);
}

ADDRINT lastRetAddr;
void CallCallback(ADDRINT retAddr) { lastRetAddr = retAddr; }

// ============================================================================
/// @brief Removes any methods that are blacklisted from the finished object
/// traces. Also removes the trace if the trace is empty after blacklisted
/// procedures are removed from it.
void RemoveBlacklistedMethods() {
  for (auto traceIt = finishedObjectTraces.begin();
       traceIt != finishedObjectTraces.end();) {
    vector<ObjectTraceEntry>* trace = *traceIt;

    for (auto entryIt = trace->begin(); entryIt != trace->end();) {
      if (blacklistedProcedures.count(entryIt->procedure)) {
        entryIt = trace->erase(entryIt);
      } else {
        entryIt++;
      }
    }

    if (trace->empty()) {
      finishedObjectTraces.erase(traceIt++);
      delete trace;
    } else {
      traceIt++;
    }
  }
}

// ============================================================================
/// @brief Alternative to RemoveBlacklistedMethods, removes the
/// entire object trace if it contains a blacklisted method.
void RemoveObjectTracesWithBlacklistedMethods() {
  for (auto traceIt = finishedObjectTraces.begin();
       traceIt != finishedObjectTraces.end();) {
    vector<ObjectTraceEntry>* trace = *traceIt;

    bool objectTraceRemoved{false};

    for (const ObjectTraceEntry& entry : *trace) {
      if (blacklistedProcedures.count(entry.procedure)) {
        objectTraceRemoved = true;
        finishedObjectTraces.erase(traceIt);
        delete trace;
        break;
      }
    }

    if (!objectTraceRemoved) {
      traceIt++;
    }
  }
}

// ============================================================================
/// @brief determine if an ADDRINT is a plausible object pointer.
bool IsPossibleObjPtr(ADDRINT ptr, ADDRINT stackPtr) {
  // TODO: something smarter! But checking if it's in an allocated region is not
  // good enough because the regions can change without triggering an image
  // load, eg with brk syscall. here's a reasonable idea: Either in a mapped
  // memory region OR in an explicitly mallocated heap zone.

  if (ptr >= stackPtr && ptr <= stackBase) {
    // LOG("in stack\n");
    return true;
  }

  // If lies within heap allocated region, address valid.
  auto it = heapAllocations.upper_bound(ptr);
  if (it != heapAllocations.begin()) {
    it--;
    if (stackPtr < it->second) {
      // LOG("in heap allocated region " + std::to_string(ptr) + ", " +
      //     std::to_string(it->first) + ", " + std::to_string(it->second) +
      //     "\n");
      return true;
    }
  }

  return false;
}

// ============================================================================
/// @brief Inserts object trace entry into the active object traces
static void InsertObjectTraceEntry(ADDRINT objPtr,
                                   const ObjectTraceEntry& entry) {
  auto objectTraceIt = activeObjectTraces.find(objPtr);

  if (objectTraceIt == activeObjectTraces.end()) {
    // Since pin doesn't support c++11 on windows we have to manage heap
    // allocations manually :(
    activeObjectTraces[objPtr] = new vector<ObjectTraceEntry>();
    objectTraceIt = activeObjectTraces.find(objPtr);
  }

  objectTraceIt->second->push_back(entry);
}

// ============================================================================
// Shadow stack helpers
// ============================================================================
/// @brief Removes the top entry in the shadow stack and decrements the
/// associated counter in the stackEntryCount. Also removes the entry in the
/// stackEntryCount if the counter is 0 to preserve memory.
static ShadowStackEntry ShadowStackRemoveAndReturnTop() {
  ShadowStackEntry stackTop = shadowStack.back();

  // Remove from shadow stack
  shadowStack.pop_back();

  // Remove from stackEntryCount
  auto& stackEntryIt = stackEntryCount.find(stackTop.returnAddr);
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
static bool IgnoreReturn(ADDRINT actualRetAddr) {
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
      stackTop = ShadowStackRemoveAndReturnTop();
    }

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
static void MethodCandidateCallback(ADDRINT procAddr, ADDRINT stackPtr,
                                    ADDRINT objPtr) {
  if (stackBase == 0) {
    stackBase = stackPtr;
  }

  // This is for debugging purposes, remove eventually
  // if (gtMethodAddrs.count(procAddr) == 0) {
  //   return;
  // }

  // stringstream ss;
  // ss << "Method called on possible objPtr: " << hex << objPtr
  //    << " with procAddr " << procAddr << " and stack ptr " << stackPtr;
  // if (procedureSymbolNames.count(procAddr)) {
  //   ss << " : " << procedureSymbolNames[procAddr];
  // }
  // ss << endl;
  // LOG(ss.str());

  // Method blacklisted, don't add to trace
  if (blacklistedProcedures.find(procAddr) != blacklistedProcedures.end()) {
    // LOG("blacklisted, not adding\n");
    return;
  }

  // Object pointer invalid, not possible method candidate
  if (!IsPossibleObjPtr(objPtr, stackPtr)) {
    blacklistedProcedures.insert(procAddr);
    // LOG("not a method\n");
    return;
  }

  // The method candidate is valid. Add to the trace and shadow stack.

  if (shadowStack.size() != 0) {
    ShadowStackEntry& trace_entry = shadowStack.back();
    // TODO insert procedure to calledProcedures
  }

  InsertObjectTraceEntry(objPtr, ObjectTraceEntry(procAddr, true));

  // should always appear just after a `call` (or should it?)
  if (lastRetAddr == static_cast<ADDRINT>(-1)) {
    // return address invalid, don't add procedure to shadow stack
    // stringstream ss;
    // ss << "WARNING: Method executed without call! Likely not a method. At  0x
    // "
    //    << hex << procAddr << endl;
    // LOG(ss.str());
  } else {
    shadowStack.push_back(ShadowStackEntry(objPtr, lastRetAddr, procAddr));
    stackEntryCount[lastRetAddr]++;
  }

  lastRetAddr = static_cast<ADDRINT>(-1);
}

// ============================================================================
/// @brief Called when a procedure is returned from. Handles popping stack entry
/// from the shadow stack and inserting an entry in the correct object trace.
/// @param returnAddr Address being returned to (not normalized).
static void RetCallback(ADDRINT returnAddr) {
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

    // The object trace shouldn't start with a return, so log a warning if this
    // is the case. Potentially do more in the future.
    auto objectTraceIt = activeObjectTraces.find(stackTop.objPtr);
    if (objectTraceIt == activeObjectTraces.end()) {
      // stringstream ss;
      // ss << "WARNING: Return being inserted into a brand new trace??? " <<
      // hex
      //    << stackTop.objPtr << endl;
      // LOG(ss.str());
    }

    InsertObjectTraceEntry(stackTop.objPtr,
                           ObjectTraceEntry(stackTop.procedure, false));
  }

  // if (stackEntryCount[returnAddr] != 0) {
  //   // if the return is in the stack somewhere, find exactly where!
  //   // pop off the top of stack until we find something matching our return
  //   // address.
  //   ShadowStackEntry stackTop(0, 0, 0);
  //   do {
  //     stackTop = shadowStack.back();
  //     shadowStack.pop_back();
  //     stackEntryCount[stackTop.returnAddr]--;
  //     // TODO: this shouldn't cause a new object trace to be created, because
  //     // the memory shouldn't be deallocated between the call and the
  //     // return...but what if it does?
  //     if (activeObjectTraces.count(stackTop.objPtr) == 0) {
  //       stringstream ss;
  //       ss << "WARNING: Return being inserted into a brand new trace??? " <<
  //       hex
  //          << stackTop.objPtr << endl;
  //       LOG(ss.str());
  //     }
  //     InsertObjectTraceEntry(stackTop.objPtr,
  //                            ObjectTraceEntry(stackTop.procedure, false));
  //   } while (stackTop.returnAddr != returnAddr && !shadowStack.empty());
  // }
}

static bool IsPossibleStackIncrease(INS ins) {
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
  assert(IMG_NumRegions(img) == 1);

  // track mapped memory regions
  for (UINT32 i = 0; i < IMG_NumRegions(img); i++) {
    mappedRegions[IMG_RegionLowAddress(img, i)] =
        IMG_RegionHighAddress(img, i) + 1;
  }

  cout << hex << IMG_Name(img) << ", " << IMG_RegionLowAddress(img, 0) << ", "
       << IMG_RegionHighAddress(img, 0) << endl;

  // TODO: what if the user specifies a routine that's already detected
  // automatically? Want to make sure we don't add it twice, but RTN isn't
  // suitable for use in a set, so we need some manual way to prevent
  // duplicates! (just compare addresses?)
  vector<RTN> mallocRtns;
  vector<RTN> freeRtns;
  if (IMG_IsMainExecutable(img)) {
    lowAddr = IMG_LowAddress(img);

    // store all procedures with symbols into the global map for debugging use.
    for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
      for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
        procedureSymbolNames[RTN_Address(rtn) - lowAddr] = RTN_Name(rtn);
      }
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

  // Insert malloc/free discovered by name into routine list

  if (RTN_Valid(discoveredMalloc)) {
    // LOG("Found malloc procedure by symbol in img " + IMG_Name(img) + "\n");
    mallocRtns.push_back(discoveredMalloc);
  } else {
    // LOG("Failed to find malloc procedure by symbol in img " + IMG_Name(img) +
    //     "\n");
  }

  if (RTN_Valid(discoveredFree)) {
    // LOG("Found free procedure by symbol in img " + IMG_Name(img) + "\n");
    freeRtns.push_back(discoveredFree);
  } else {
    // LOG("Failed to find free procedure by symbol in img " + IMG_Name(img) +
    //     "\n");
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

  cout << "Finished all object traces" << endl;

  RemoveBlacklistedMethods();

  cout << dec << "found " << finishedObjectTraces.size()
       << " valid unique object traces" << endl;

  string objectTracesPathStr = objectTracesPath.Value().c_str();

  ofstream os(objectTracesPathStr.c_str());
  // TODO: use a more structured trace format...currently all traces shoved into
  // files, which are broken up to avoid making files that are too large.
  for (const vector<ObjectTraceEntry>* trace : finishedObjectTraces) {
    for (const ObjectTraceEntry& entry : *trace) {
      os << entry.procedure << " " << entry.isCall << endl;
    }
    os << endl;
  }

  os.close();
  os.open(std::string(objectTracesPathStr + "-name-map").c_str());
  for (const auto& it : procedureSymbolNames) {
    os << it.first << " " << it.second << endl;
  }

  string gtMethodsInstrumentedStr = gtMethodsInstrumentedPath.Value().c_str();
  os.close();
  os.open(gtMethodsInstrumentedStr.c_str());

  float coverage = static_cast<float>(gtCalledMethods.size()) /
                   static_cast<float>(gtMethodAddrs.size());
  os << "Ground truth methods: " << gtMethodAddrs.size()
     << ", Called ground truth methods: " << gtCalledMethods.size()
     << ", Coverage (%): " << coverage << endl
     << "=====" << endl;
  for (ADDRINT addr : gtCalledMethods) {
    os << addr << endl;
  }

  cout << "Done! Exiting normally." << endl;
}

int main(int argc, char** argv) {
  ParsePregame();

  PIN_InitSymbols();  // this /is/ necessary for debug symbols, but somehow it
  // doesn't have an entry in the PIN documentation? (though
  // its name is referenced in a few places).
  PIN_Init(argc, argv);
  IMG_AddInstrumentFunction(InstrumentImage, NULL);
  IMG_AddUnloadFunction(InstrumentUnloadImage, NULL);
  INS_AddInstrumentFunction(InstrumentInstruction, NULL);
  PIN_AddFiniFunction(Fini, NULL);

  // Start program, never returns
  PIN_StartProgram();
  return 0;
}
