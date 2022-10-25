#include <iostream>
#include <fstream>
#include <unordered_set>
#include <string>
#include <cstdio>

#include "pin.h"

using namespace std;

// pregame output is parsed into these global variables
unordered_set<uint64_t> method_candidate_addrs;
uint64_t minAddr; // base offset of the executable image what the fuck si going on why is it so laggy oercuheorcuheorchuoercuh
string binaryPath;

void ParsePregame(const string &path) {
	ifstream is(path); // TODO: error checking
	if (!is) {
		cerr << "Failed to open " << path << "." << endl;
		exit(1);
	}
	for (string curLine; curLine = getline(is, curLine); ) {
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

void InstrumentInstruction(INS ins, void *) {
	// We want to instrument:
	// + Calls to candidate methods.
	// + Calls to memory allocation procedures (malloc, free, new, new[], delete, delete[])
	// + Upward motion of the stack pointer.

	// First, candidate methods:
	if ()
}

int main(int argc, char **argv) {
	ParsePregame("./pregame-output"); // TODO: command line option for filename
	PIN_Init(argc, argv);
	INS_AddInstrumentFunction(InstrumentInstruction, NULL);
	// TODO: do we need to do anything explicit to close the file when we're done with it? Normally RAII would take care of that, but I'm concerned that the StartProgram call somehow transports us to a whole other dimension maybe the cleanup for `main` never happens

	// PIN_AddFiniFunction(, NULL);
}
