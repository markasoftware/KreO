#include <iostream>
#include <fstream>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdio>
#include <cassert>

#include "pin.H"

using namespace std;

// malloc names -- not sure how correct it is, but it's how it's done in malloctrace.c from pin examples
#ifdef TARGET_MAC
#define MALLOC_SYMBOL "_malloc"
#define FREE_SYMBOL "_free"
#else
#define MALLOC_SYMBOL "malloc"
#define FREE_SYMBOL "free"
#endif

// custom structures
struct ObjectTraceEntry {
	ADDRINT procedure;
	bool isCall; // else, is return
	// TODO: look into calledMethods, what exactly is it for?
};

struct ShadowStackEntry {
	ADDRINT returnAddr;
	ADDRINT procedure;
	ADDRINT objPtr;
};

KNOB<string> configPath(KNOB_MODE_WRITEONCE, "pintool", "config", "config.json", "Path to configuration json file (same as for pregame and postgame).");
KNOB<string> methodCandidatesPath(KNOB_MODE_WRITEONCE, "pintool", "method-candidates", "method-candidates", "Path to method candidates file.");
KNOB<string> objectTracesPath(KNOB_MODE_WRITEONCE, "pintool", "object-traces", "object-traces", "Path to object traces output file.");
// TODO: make these knobs:
vector<ADDRINT> mallocProcedures;
vector<ADDRINT> freeProcedures;

unordered_set<ADDRINT> methodCandidateAddrs;
// TODO: potential optimization: Don't store finishedObjectTraces separately from activeObjectTraces; instead just have a single map<ADDRINT, vector<TraceEntry>>, and then insert special trace objects indicating when memory has been deallocated, allowing to easily split them at the end. Problem then is that multiple "splits" may be inserted, so theoretically an old trace whose memory keeps getting allocated and deallocated could be problematic...but does that even happen?
vector<vector<ObjectTraceEntry>> finishedObjectTraces;
map<ADDRINT, vector<ObjectTraceEntry>> activeObjectTraces;
vector<ShadowStackEntry> methodStack; // "shadow stack". Each entry is (return addr, method addr)
unordered_map<ADDRINT, int> stackEntryCount; // number of times each return address appears in the method stack.
unordered_map<ADDRINT, ADDRINT> heapAllocations; // map starts of regions to (one past the) ends of regions.
map<ADDRINT, ADDRINT> mappedRegions; // ^^^, but for mapped areas of memory (images). Used to determine valid object ptrs
ADDRINT lowAddr; // base offset of the executable image what the fuck si going on why is it so laggy oercuheorcuheorchuoercuh
unordered_map<ADDRINT, string> procedureSymbolNames; // can't use RTN methods during Fini, so we explicitly store all the symbol names here (for debugging)

void ParsePregame() {
	// TODO

	ifstream methodCandidatesStream(methodCandidatesPath.Value());
	ADDRINT methodCandidate;
	while (methodCandidatesStream >> methodCandidate >> ws) {
		methodCandidateAddrs.insert(methodCandidate);
	}
}

void EndObjectTracesInRegion(ADDRINT regionStart, ADDRINT regionEnd) {
	// lower_bound means that the argument is the lower bound and that the returned iterator points to an element at least as great as the argument.
	// And, as you'd expect, upper_bound points /after/ the position you really want, to make it easy to use in the end condition of a loop..

	auto firstTrace = activeObjectTraces.lower_bound(regionStart);
	auto lastTrace = activeObjectTraces.end();
	auto it = firstTrace;
	for (; it != lastTrace && it->first < regionEnd; it = activeObjectTraces.erase(it)) {
		// (i did investigate a bit and i'm pretty sure it doesn't copy, because it seems on gcc at least that `map` internally stores `pair`s at tree leaves, so I don't think this'll copy anything)
		cerr << "Deallocated, flushing trace for " << hex << it->first << endl;
		finishedObjectTraces.push_back(std::move(it->second));
	}
}

///////////////////////
// ANALYSIS ROUTINES //
///////////////////////

ADDRINT mallocSize; // saves arg passed to most recent malloc invocation
void MallocBeforeCallback(ADDRINT size) {
	mallocSize = size;
}
void MallocAfterCallback(ADDRINT regionStart) {
#ifndef SUPPRESS_MALLOC_ERRORS
	if (heapAllocations.find(regionStart) != heapAllocations.end()) {
		cerr << "PINTOOL WARNING: Malloc'ing a pointer that was already malloc'ed! Maybe too many malloc procedures specified?" << endl;
		// TODO: could debug even further by searching for if the pointer lies within an allocated region.
	}
#endif

	cerr << "Malloc @ " << hex << regionStart << endl;
	heapAllocations[regionStart] = regionStart + mallocSize;
}

void FreeCallback(ADDRINT freedRegionStart) {
	cerr << "Free @ " << hex << freedRegionStart << endl;
	if (freedRegionStart == 0) { // for whatever reason, real programs seem to be doing this??
		cerr << "WARNING: Freed nullptr?" << endl;
		return;
	}
#ifndef SUPPRESS_MALLOC_ERRORS
	if (heapAllocations.find(freedRegionStart) == heapAllocations.end()) {
		cerr << "PINTOOL WARNING: Invalid pointer freed! Check the program-under-test." << endl;
		return;
	}
#endif

	auto freedRegionIt = heapAllocations.find(freedRegionStart);
	ADDRINT freedRegionEnd = freedRegionIt->second;
	EndObjectTracesInRegion(freedRegionStart, freedRegionEnd);
	heapAllocations.erase(freedRegionIt);
}

// a stack pointer change is interesting if it moves the pointer to the lowest known address, or moves it up past an active object trace.
// Possible microoptimization: For `add` operations on the stack pointer, only check if it's increased past an active object pointer, and for `sub` operations, only check if it's decreased below the lowest known.
// TODO: somehow track the lowest active objPtr in the stack. Could be done by checking if an objptr is above the stack during method calls. That way we don't have to call EndObjectTracesInRegion so frequently.
ADDRINT lastStackPtr;
void StackIncreaseBeforeCallback(ADDRINT stackPtr) {
	lastStackPtr = stackPtr;
}
bool StackIncreasePredicate(ADDRINT stackPtr) {
	return stackPtr > lastStackPtr;
}
void StackIncreaseCallback(ADDRINT stackPtr) {
	EndObjectTracesInRegion(lastStackPtr, stackPtr);
}


ADDRINT lastRetAddr;
void CallCallback(ADDRINT retAddr) {
	lastRetAddr = retAddr;
}
// determine if an ADDRINT is a plausible object pointer.
bool IsPossibleObjPtr(ADDRINT ptr) {
	// TODO: something smarter! But checking if it's in an allocated region is not good enough because the regions can change without triggering an image load, eg with brk syscall.
	// here's a reasonable idea: Either in a mapped memory region OR in an explicitly mallocated heap zone.
	return ptr >= lowAddr;

 // 	// currently, just check if it's in a mapped memory region.
 // 	// Theoretically, can be more precise by tracking, for example, if it's in a section that makes sense for objects to live in, and further by checking for example that if it's in the heap it's been allocated by malloc. But this stuff often is platform-specific and could break under obfuscation.
 // 	auto it = mappedRegions.upper_bound(ptr);
 // // TODO: check the time complexity of this! Based on different sources, it seems it could either be O(1), O(log n), or O(n)!
 // 	return it != mappedRegions.begin() && --it != mappedRegions.begin() && ptr < it->second;
 // 	// TODO: blacklisting: If a method is called once with an invalid object pointer, then it
 // 	// needs to be permanently blacklisted. I think the Lego paper describes this.
}
void MethodCandidateCallback(ADDRINT procAddr, ADDRINT objPtr) {
	if (!IsPossibleObjPtr(objPtr)) {
		return;
	}
	cerr << "Method called on possible objPtr: " << hex << objPtr << endl;
	// we're a method candidate! Add to the trace and shadow stack.

	// should always appear just after a `call` (or should it?)
	//assert(lastRetAddr != (ADDRINT)-1);
	ObjectTraceEntry entry = {
		.procedure = procAddr,
		.isCall = true,
	};
	activeObjectTraces[objPtr].push_back(entry);
	if (lastRetAddr == (ADDRINT)-1) {
		cerr << "WARNING: Method executed without call! Likely not a method. At 0x" << hex << procAddr << endl;
	} else {
		methodStack.push_back({
				.returnAddr = lastRetAddr,
				.procedure = procAddr,
				.objPtr = objPtr
			});
		stackEntryCount[lastRetAddr]++;
	}
	lastRetAddr = (ADDRINT)-1;
}

void RetCallback(ADDRINT returnAddr) {
	// TODO: maybe do this as an InsertIfCall? Probably won't help though because map lookup probably can't be inlined.
	// TODO: perhaps include a compile time flag to use a simpler implementation where we assume exact correspondence between `call` and `ret` to speed things up on more predictable software

	// after some godbolt experimentation: GCC is capable of inlining hashtable access, but the hash table access function has branches.
	// if the ret is not in the stack, then maybe there was some manual push-jump stuff going on, so we ignore it.
	if (stackEntryCount[returnAddr] != 0) {
		// if the return is in the stack somewhere, find exactly where!
		// pop off the top of stack until we find something matching our return address. 
		ShadowStackEntry stackTop;
		do {
			stackTop = methodStack.back();
			methodStack.pop_back();
			stackEntryCount[stackTop.returnAddr]--;
			// TODO: this shouldn't cause a new object trace to be created, because the memory shouldn't be deallocated between the call and the return...but what if it does?
			if (activeObjectTraces.count(stackTop.objPtr) == 0) {
				cerr << "WARNING: Return being inserted into a brand new trace??? " << hex << stackTop.objPtr << endl;
			}
			activeObjectTraces[stackTop.objPtr].push_back({
					.procedure = stackTop.procedure,
					.isCall = false,
				});
		} while (stackTop.returnAddr != returnAddr && !methodStack.empty());
	}
}

///////////////////
// INSTRUMENTERS //
///////////////////

bool IsPossibleStackIncrease(INS ins) {
	// TODO: check for other instructions we might want to exclude, or handle specially
	// (branches can't modify sp, right?)

	// There /is/ one instruction which modifies the stack pointer which we intentionally
	// exclude here -- `ret`. While it moves the stack pointer up by one, it shouldn't matter
	// unless you are jumping to an object pointer -- and in that case, we have larger problems!

	if (INS_IsSub(ins)) {
		return false;
	}
	
	UINT32 operandCount = INS_OperandCount(ins);
	for (UINT32 opIdx = 0; opIdx < operandCount; opIdx++) {
		if (INS_OperandWritten(ins, opIdx)
		    && INS_OperandReg(ins, opIdx) == REG_STACK_PTR) {
			return true;
		}
	}
	return false;
}

void InstrumentInstruction(INS ins, void *) {
	// We want to instrument:
	// + Calls to candidate methods, to add to the trace.
	// + Return instructions, to add to the trace.
	// + Upward motion of the stack pointer, to track allocated memory and perhaps end traces.

	ADDRINT insRelAddr = INS_Address(ins) - lowAddr;
	if (methodCandidateAddrs.count(insRelAddr) == 1) {
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)MethodCandidateCallback,
			       IARG_ADDRINT, insRelAddr, IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END); // TODO: are we really allowed to put FUNCARG on a non-routine instrumentation?
	}

	// The following types of instructions should be mutually exclusive.
	bool alreadyInstrumented = false;
	if (IsPossibleStackIncrease(ins)) {
		assert(!alreadyInstrumented);
		alreadyInstrumented = true;

		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)StackIncreaseBeforeCallback,
			       IARG_REG_VALUE, REG_STACK_PTR, IARG_END);
		INS_InsertIfCall(ins, IPOINT_AFTER, (AFUNPTR)StackIncreasePredicate,
				 IARG_REG_VALUE, REG_STACK_PTR, IARG_END);
		INS_InsertThenCall(ins, IPOINT_AFTER, (AFUNPTR)StackIncreaseCallback,
				   IARG_REG_VALUE, REG_STACK_PTR, IARG_END);
	}

	if (INS_IsRet(ins)) { // TODO: do we need to handle farret?
		assert(!alreadyInstrumented);
		alreadyInstrumented = true;

                // docs say `ret` implies control flow, but just want to be sure, since this a
		// precondition for IARG_BRANCH_TARGET_ADDR but idk if that does an internal assert.
		assert(INS_IsControlFlow(ins));
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)RetCallback,
			       IARG_BRANCH_TARGET_ADDR, IARG_END);
	}

	if (INS_IsCall(ins)) {
		assert(!alreadyInstrumented);
		alreadyInstrumented = true;

		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)CallCallback,
			       IARG_ADDRINT, INS_NextAddress(ins), IARG_END);
	}
}

void InstrumentImage(IMG img, void *) {
	// most of the work here is finding potential malloc and free procedures and instrumenting
	// them appropriately.

	// track mapped memory regions
	for (UINT32 i = 0; i < IMG_NumRegions(img); i++) {
		mappedRegions[IMG_RegionLowAddress(img, i)] = IMG_RegionHighAddress(img, i)+1;
	}

	// TODO: what if the user specifies a routine that's already detected automatically? Want to make sure we don't add it twice, but RTN isn't suitable for use in a set, so we need some manual way to prevent duplicates! (just compare addresses?)
	vector<RTN> mallocRtns;
	vector<RTN> freeRtns;
	if (IMG_IsMainExecutable(img)) {
		lowAddr = IMG_LowAddress(img);

		// store all procedures with symbols into the global map for debugging use.
		for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
			for(RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
				procedureSymbolNames[RTN_Address(rtn)] = RTN_Name(rtn);
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

	RTN discoveredMalloc = RTN_FindByName(img, MALLOC_SYMBOL); // TODO: look more into what the malloc symbol is named on different platforms.
	RTN discoveredFree = RTN_FindByName(img, FREE_SYMBOL); // TODO: it seems that `free` in one library calls `free` in another library (at least on gnu libc linux), because 
	if (RTN_Valid(discoveredMalloc)) {
		// TODO: figure out if we can hook into pin's logging system?
		cerr << "Found malloc procedure by symbol!" << endl;
		mallocRtns.push_back(discoveredMalloc);
	}
	if (RTN_Valid(discoveredFree)) {
		cerr << "Found free procedure by symbol!" << endl;
		freeRtns.push_back(discoveredFree);
	}
	for (RTN mallocRtn : mallocRtns) {
		RTN_Open(mallocRtn);
		// save first argument
		RTN_InsertCall(mallocRtn, IPOINT_BEFORE, (AFUNPTR)MallocBeforeCallback,
			       IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
		RTN_InsertCall(mallocRtn, IPOINT_AFTER, (AFUNPTR)MallocAfterCallback,
			       IARG_FUNCRET_EXITPOINT_VALUE, IARG_END);
		RTN_Close(mallocRtn);
	}
	for (RTN freeRtn : freeRtns) {
		RTN_Open(freeRtn);
		RTN_InsertCall(freeRtn, IPOINT_BEFORE, (AFUNPTR)FreeCallback,
			       IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END);
		RTN_Close(freeRtn);
	}
}

void InstrumentUnloadImage(IMG img, void *) {
	// remove regions from mapped memory tracker
	for (UINT32 i = 0; i < IMG_NumRegions(img); i++) {
		mappedRegions.erase(IMG_RegionLowAddress(img, i));
	}
}

void Fini(INT32 code, void *) {
	cerr << "Program run completed, writing object traces to disk..." << endl;
	// end all in-progress traces, then print all to disk.

	for (auto &pair : activeObjectTraces) {
		if (!pair.second.empty()) {
			finishedObjectTraces.push_back(move(pair.second));
		}
	}

	ofstream os(objectTracesPath.Value());
	// TODO: use a more structured trace format.
	for (const vector<ObjectTraceEntry> &trace : finishedObjectTraces) {
		for (const ObjectTraceEntry &entry : trace) {
			if (procedureSymbolNames.count(lowAddr + entry.procedure)) {
				os << procedureSymbolNames[lowAddr + entry.procedure] << " ";
			}
			os << entry.procedure << " " << entry.isCall << endl;
		}
		os << endl;
	}
	cerr << "Done! Exiting normally." << endl;
}

int main(int argc, char **argv) {
	ParsePregame();

	PIN_InitSymbols(); // this /is/ necessary for debug symbols, but somehow it doesn't have an entry in the PIN documentation? (though its name is referenced in a few places).
	PIN_Init(argc, argv);
	IMG_AddInstrumentFunction(InstrumentImage, NULL);
	IMG_AddUnloadFunction(InstrumentUnloadImage, NULL);
	INS_AddInstrumentFunction(InstrumentInstruction, NULL);
	PIN_AddFiniFunction(Fini, NULL);
	PIN_StartProgram();
	return 0;
}
