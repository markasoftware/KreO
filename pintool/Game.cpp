#include <iostream>
#include <fstream>
#include <unordered_set>
#include <map>
#include <vector>
#include <string>
#include <cstdio>

#include "pin.H"
#include "vendor/json5/json5_input.h"

using namespace std;

// custom structures
struct ObjectTraceEntry {
	ADDRINT procedure;
	bool isCall; // else, is return
	// TODO: look into calledMethods, what exactly is it for?
};

// pregame output is parsed into these global variables
unordered_set<ADDRINT> methodCandidateAddrs;
map<ADDRINT, vector<ObjectTraceEntry>> activeObjectTraces;
ADDRINT lowestActiveObjectPtr; // speeds up stack pointer analysis
ADDRINT lowAddr; // base offset of the executable image what the fuck si going on why is it so laggy oercuheorcuheorchuoercuh
string binaryPath;

void ParsePregame(const string &path) {
	ifstream is(path); // TODO: error checking
	if (!is) {
		cerr << "Failed to open " << path << "." << endl;
		exit(1);
	}
	for (string curLine; getline(is, curLine); ) {
		if (curLine == "[binary]") {
			getline(is, binaryPath);
			break;
		}
		if (curLine == "[method-candidates]") {
			long numMethods;
			cin >> numMethods >> ws;
			while (numMethods --> 0) {
				uint64_t methodAddr;
				cin >> methodAddr >> ws; // TODO: check whether this handles the 0x and hex mode properly. Or maybe just do everything in decimal because it's not meant to be human-readable anyway!
				method_candidate_addrs.insert(methodAddr);
			}
		}
	}
}

///////////////////////
// ANALYSIS ROUTINES //
///////////////////////



void StackChangePredicate(ADDRINT stackPointer) {
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
	// + Calls to memory allocation procedures (malloc, free, new, new[], delete, delete[])
	// + Upward motion of the stack pointer.

	if (IsPossibleStackIncrease(ins)) {
		INS_InsertIfCall(ins, IPOINT_AFTER,
				 (AFUNPTR)StackChangePredicate, IARG_REG_VALUE, REG_STACK_PTR, IARG_END);
		INS_InsertCall(ins, IPOINT_AFTER,
			       (AFUNPTR)StackChangeAnalysis, IARG_REG_VALUE, REG_STACK_PTR, IARG_END);
	}

	ADDRINT addr = INS_Address(ins);

	// instrument allocators and deallocators
	if (IsMallocAddr(addr))
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)MallocAnalysis, IARG_END);
	if (IsFreeAddr(addr))
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)FreeAnalysis, IARG_END);
	if (IsReallocAddr(addr))
		INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)NewAnalysis, IARG_END);
}

void InstrumentImage(IMG img, void *) {
	if (IMG_IsMainExecutable(img)) {
		lowAddr = IMG_LowAddress(img);
	}
}

int main(int argc, char **argv) {
	ParsePregame(string("./pregame-output")); // TODO: command line option for filename
	PIN_Init(argc, argv);
	IMG_AddInstrumentFunction(InstrumentImage, NULL);
	INS_AddInstrumentFunction(InstrumentInstruction, NULL);
	// TODO: do we need to do anything explicit to close the file when we're done with it? Normally RAII would take care of that, but I'm concerned that the StartProgram call somehow transports us to a whole other dimension maybe the cleanup for `main` never happens

	// PIN_AddFiniFunction(, NULL);
}
