#include <iostream>
#include <fstream>
#include <unordered_set>
#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdio>

#include "pin.H"
#include "types_vmapi.PH"
#include "vendor/json5/json5_input.h"

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
unordered_map<ADDRINT, ADDRINT> heapAllocations; // map starts of regions to (one past the) ends of regions.
ADDRINT lowestActiveObjectPtr = (ADDRINT)-1; // speeds up stack pointer analysis
ADDRINT lowAddr; // base offset of the executable image what the fuck si going on why is it so laggy oercuheorcuheorchuoercuh

void ParsePregame() {
	json5::from_file(configPath, config);

	// TODO

	ifstream methodCandidatesStream()
	string methodCandidate;
	while (getline(methodCandidatesStream, methodCandidate)) {
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
	}
	// It's also possible to erase element-by-element as we are traversing, but why?
	activeObjectTraces.erase(firstTrace, it);

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

void FreeAfterCallback(ADDRINT freedRegionStart) {
#ifndef SUPPRESS_MALLOC_ERRORS
	if (heapAllocations.find(freedRegionStart) == heapAllocations.end()) {
		cerr << "PINTOOL WARNING: Invalid pointer freed! Check the program-under-test." << endl;
	}
#endif

	EndObjectTracesInRegion(freedRegionStart, heapAllocations[freedRegionStart]);
}

void StackChangePredicateCallback(ADDRINT stackPointer) {
	return stackPointer > lowestActiveObjectPtr;
}

///////////////////
// INSTRUMENTERS //
///////////////////

// does the given instruction possibly move the stack pointer up?
// heuristic: Does the instruction write to the stack and is not a subtraction?
bool IsPossibleStackIncrease(INS ins) {
	// TODO: check for other instructions we might want to exclude, or handle specially
	// (branches can't modify sp, right?)
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
	// + Calls to candidate methods.
	// + Upward motion of the stack pointer.

	if (IsPossibleStackIncrease(ins)) {
		INS_InsertIfCall(ins, IPOINT_AFTER, (AFUNPTR)StackChangePredicateCallback,
				 IARG_REG_VALUE, REG_STACK_PTR, IARG_END);
		INS_InsertCall(ins, IPOINT_AFTER, (AFUNPTR)EndObjectTracesInRegion,
			       IARG_ADDRINT_VALUE, 0, IARG_REG_VALUE, REG_STACK_PTR, IARG_END);
	}

	ADDRINT addr = INS_Address(ins);
	if (methodCandidateAddrs.count(addr) == 1) {
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)MethodCandidateCallback,
			       IARG_FUNCARG_ENTRYPOINT_VALUE, 0, IARG_END); // TODO: are we really allowed to put FUNCARG on a non-routine instrumentation?
	}
}

void InstrumentImage(IMG img, void *) {
	// most of the work here is finding potential malloc and free procedures and instrumenting
	// them appropriately.

	vector<RTN> mallocRtns;
	vector<RTN> freeRtns;
	if (IMG_IsMainExecutable(img)) {
		lowAddr = IMG_LowAddress(img);
		for (auto i = 0; i < config.mallocProcedures.size(); i++) {
			// create routine if does not exist
			if (!RTN_Valid(RTN_FindByAddress(mallocAddr + lowAddr))) {
				string name = "malloc_custom_" + string(i);
				RTN_CreateAt(mallocAddr + lowAddr, name);
			}
			mallocRtns.push_back(RTN_FindByAddress(mallocAddr + lowAddr));
		}
		for (auto i = 0; i < config.freeProcedures.size(); i++) {
			// create routine if does not exist
			if (!RTN_Valid(RTN_FindByAddress(freeAddr + lowAddr))) {
				string name = "free_custom_" + string(i);
				RTN_CreateAt(freeAddr + lowAddr, name);
			}
			freeRtns.push_back(RTN_FindByAddress(freeAddr + lowAddr));
		}
	}

	RTN discovered_malloc = RTN_FindByName(img, MALLOC_SYMBOL); // TODO: look more into what the malloc symbol is named on different platforms.
	RTN discovered_free = RTN_FindByName(img, FREE_SYMBOL);
	if (RTN_VALID(malloc)) {
#ifdef PINTOOL_DEBUG
		// TODO: figure out if we can hook into pin's logging system?
		cerr << "Found malloc procedure by symbol!" << endl;
#endif
		mallocRtns.push_back(discovered_malloc);
	}
	if (RTN_VALID(discovered_free)) {
#ifdef PINTOOL_DEBUG
		cerr << "Found free procedure by symbol!" << endl;
#endif
		freeRtns.push_back(discovered_free);
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

// TODO: use a different object trace format
void PrintObjectTrace(const ostream &os, const vector<ObjectTraceEntry> &trace) {
	for (const ObjectTraceEntry &entry : trace) {
		os << entry.procedure << " " << (int)isCall << endl;
	}
	os << endl;
}

void Fini(INT32 code, void *) {
	// attitude of the knife
	for (auto &pair : activeObjectTraces) {
		finishedObjectTraces.push_back(move(pair.second));
	}

	ofstream os(config.objectTracesPath);
	for (const vector<ObjectTraceEntry> &trace : finishedObjectTraces) {
		PrintObjectTrace(os, trace);
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
