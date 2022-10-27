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
#include "vendor/json5/json5_input.hpp"

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

struct Config {
	string methodCandidatesPath = "method-candidates";
	string objectTracesPath = "object-traces";
	vector<ADDRINT> mallocProcedures;
	vector<ADDRINT> freeProcedures;

	JSON5_MEMBERS(methodCandidatesPath, objectTracesPath, mallocProcedures, freeProcedures);
};

KNOB<string> configPath(KNOB_MODE_WRITEONCE, "pintool", "config", "config.json", "Path to configuration json file (same as for pregame and postgame).");

Config config;
unordered_set<ADDRINT> methodCandidateAddrs;
// TODO: potential optimization: Don't store finishedObjectTraces separately from activeObjectTraces; instead just have a single map<ADDRINT, vector<TraceEntry>>, and then insert special trace objects indicating when memory has been deallocated, allowing to easily split them at the end. Problem then is that multiple "splits" may be inserted, so theoretically an old trace whose memory keeps getting allocated and deallocated could be problematic...but does that even happen?
vector<vector<ObjectTraceEntry>> finishedObjectTraces;
map<ADDRINT, vector<ObjectTraceEntry>> activeObjectTraces;
vector<pair<ADDRINT, ADDRINT>> methodStack; // "shadow stack". Each entry is (return addr, method addr)
unordered_map<ADDRINT, int> stackEntryCount; // number of times each return address appears in the method stack.
unordered_map<ADDRINT, ADDRINT> heapAllocations; // map starts of regions to (one past the) ends of regions.
ADDRINT lowestActiveObjectPtr = (ADDRINT)-1; // speeds up stack pointer analysis
ADDRINT lowAddr; // base offset of the executable image what the fuck si going on why is it so laggy oercuheorcuheorchuoercuh

void ParsePregame() {
	json5::from_file(configPath.Value(), config);

	// TODO

	ifstream methodCandidatesStream(config.methodCandidatesPath);
	ADDRINT methodCandidate;
	while (methodCandidatesStream >> methodCandidate >> ws) {
		methodCandidateAddrs.insert(methodCandidate);
	}
}

void EndObjectTracesInRegion(ADDRINT regionStart, ADDRINT regionEnd) {
	// lower_bound means that the argument is the lower bound and that the returned iterator points to an element at least as great as the argument.
	// And, as you'd expect, upper_bound points /after/ the position you really want, to make it easy to use in the end condition of a loop..

	// microoptimization; we know there's nothing negative in there
	auto firstTrace = regionStart == 0 ? activeObjectTraces.begin() : activeObjectTraces.lower_bound(regionStart);
	auto lastTrace = activeObjectTraces.end();
	auto it = firstTrace;
	for (; it != lastTrace && it->first < regionEnd; it++) {
		// TODO: double check that this doesn't copy the object trace.
		// (but i did investigate a bit and i'm pretty sure it doesn't copy, because it seems on gcc at least that `map` internally stores `pair`s at tree leaves)
		finishedObjectTraces.push_back(std::move(it->second));
		it->second = {};
	}

	if (it == lastTrace) {
		lowestActiveObjectPtr = (ADDRINT)-1;
	} else {
		lowestActiveObjectPtr = activeObjectTraces.begin()->first;
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

	heapAllocations[regionStart] = regionStart + mallocSize;
}

void FreeCallback(ADDRINT freedRegionStart) {
#ifndef SUPPRESS_MALLOC_ERRORS
	if (heapAllocations.find(freedRegionStart) == heapAllocations.end()) {
		cerr << "PINTOOL WARNING: Invalid pointer freed! Check the program-under-test." << endl;
	}
#endif

	EndObjectTracesInRegion(freedRegionStart, heapAllocations[freedRegionStart]);
}

bool StackChangePredicateCallback(ADDRINT stackPointer) {
	return stackPointer > lowestActiveObjectPtr;
}


ADDRINT lastRetAddr;
void CallCallback(ADDRINT retAddr) {
	lastRetAddr = retAddr;
}
// determine if an ADDRINT is a plausible object pointer.
bool IsObjPtr(ADDRINT objptr) {
	return true; // TODO: Check whether it points to an allocated memory region. But that's a
		     // bit tricky: Could be stack, heap, static variable, probably some other
		     // places I'm missing.
	// TODO: possible improvement: If a method is called once with an invalid object pointer,
	// then it needs to be permanently blacklisted. I think the Lego paper describes this.
}
void MethodCandidateCallback(ADDRINT procAddr, ADDRINT objptr) {
	if (!IsObjPtr(objptr)) {
		return;
	}
	// we're a method candidate! Add to the trace and shadow stack.
	assert(lastRetAddr != (ADDRINT)-1);
	ObjectTraceEntry entry = {
		.procedure = procAddr,
		.isCall = true,
	};
	activeObjectTraces[objptr].push_back(entry);
	methodStack.push_back( {lastRetAddr, procAddr} );
	stackEntryCount[lastRetAddr]++; // I wonder if there's a faster way to increment, since [] isn't as fast as possible
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
		pair<ADDRINT, ADDRINT> stackTop;
		do {
			stackTop = methodStack.back();
			methodStack.pop_back();
			activeObjectTraces[stackTop.first].push_back({
					.procedure = stackTop.second,
					.isCall = false,
				});
		} while (stackTop.first != returnAddr && !methodStack.empty());
	}
}

///////////////////
// INSTRUMENTERS //
///////////////////

// does the given instruction possibly move the stack pointer up?
// heuristic: Does the instruction write to the stack and is not a subtraction?
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

	// I can't think of any case when multiple of these would apply, so let's assert that it can't happen.
	bool alreadyInstrumented = false;

	if (IsPossibleStackIncrease(ins)) {
		assert(!alreadyInstrumented);
		alreadyInstrumented = true;

		INS_InsertIfCall(ins, IPOINT_AFTER, (AFUNPTR)StackChangePredicateCallback,
				 IARG_REG_VALUE, REG_STACK_PTR, IARG_END);
		INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR)EndObjectTracesInRegion,
			       IARG_ADDRINT, 0, IARG_REG_VALUE, REG_STACK_PTR, IARG_END);
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
	if (methodCandidateAddrs.count(INS_Address(ins)) == 1) {
		assert(!alreadyInstrumented); // TODO: it's possible for this to fail if the first instr in a method is `call`. Not the end of the world; we just need to double check that analysis functions are called in the order they're inserted, and that we have the right order here.
		alreadyInstrumented = true;

		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)MethodCandidateCallback,
			       IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END); // TODO: are we really allowed to put FUNCARG on a non-routine instrumentation?
	}
}

void InstrumentImage(IMG img, void *) {
	// most of the work here is finding potential malloc and free procedures and instrumenting
	// them appropriately.

	// TODO: what if the user specifies a routine that's already detected automatically? Want to make sure we don't add it twice, but RTN isn't suitable for use in a set, so we need some manual way to prevent duplicates! (just compare addresses?)
	vector<RTN> mallocRtns;
	vector<RTN> freeRtns;
	if (IMG_IsMainExecutable(img)) {
		lowAddr = IMG_LowAddress(img);
		for (size_t i = 0; i < config.mallocProcedures.size(); i++) {
			// create routine if does not exist
			ADDRINT mallocAddr = config.mallocProcedures[i];
			if (!RTN_Valid(RTN_FindByAddress(mallocAddr + lowAddr))) {
				string name = "malloc_custom_" + to_string(i);
				RTN_CreateAt(mallocAddr + lowAddr, name);
			}
			mallocRtns.push_back(RTN_FindByAddress(mallocAddr + lowAddr));
		}
		for (size_t i = 0; i < config.freeProcedures.size(); i++) {
			// create routine if does not exist
			ADDRINT freeAddr = config.freeProcedures[i];
			if (!RTN_Valid(RTN_FindByAddress(freeAddr + lowAddr))) {
				string name = "free_custom_" + to_string(i);
				RTN_CreateAt(freeAddr + lowAddr, name);
			}
			freeRtns.push_back(RTN_FindByAddress(freeAddr + lowAddr));
		}
	}

	RTN discoveredMalloc = RTN_FindByName(img, MALLOC_SYMBOL); // TODO: look more into what the malloc symbol is named on different platforms.
	RTN discoveredFree = RTN_FindByName(img, FREE_SYMBOL);
	if (RTN_Valid(discoveredMalloc)) {
#ifdef PINTOOL_DEBUG
		// TODO: figure out if we can hook into pin's logging system?
		cerr << "Found malloc procedure by symbol!" << endl;
#endif
		mallocRtns.push_back(discoveredMalloc);
	}
	if (RTN_Valid(discoveredFree)) {
#ifdef PINTOOL_DEBUG
		cerr << "Found free procedure by symbol!" << endl;
#endif
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

void Fini(INT32 code, void *) {
	// end all in-progress traces, then print all to disk.

	for (auto &pair : activeObjectTraces) {
		if (!pair.second.empty()) {
			finishedObjectTraces.push_back(move(pair.second));
		}
	}

	ofstream os(config.objectTracesPath);
	// TODO: use a more structured trace format.
	for (const vector<ObjectTraceEntry> &trace : finishedObjectTraces) {
		for (const ObjectTraceEntry &entry : trace) {
			os << entry.procedure << " " << entry.isCall << endl;
		}
		os << endl;
	}
}

int main(int argc, char **argv) {
	ParsePregame();

	PIN_Init(argc, argv);
	IMG_AddInstrumentFunction(InstrumentImage, NULL);
	INS_AddInstrumentFunction(InstrumentInstruction, NULL);
	PIN_AddFiniFunction(Fini, NULL);
	PIN_StartProgram();
	return 0;
}
