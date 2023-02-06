#include <rose.h>

#include <Rose/BinaryAnalysis/InstructionSemantics/PartialSymbolicSemantics.h>
#include <Rose/BinaryAnalysis/Partitioner2/DataFlow.h>
#include <Rose/BinaryAnalysis/Partitioner2/Partitioner.h>
#include <Rose/BinaryAnalysis/Partitioner2/Function.h>
#include <Rose/BinaryAnalysis/Partitioner2/Engine.h>
#include <Rose/BinaryAnalysis/MemoryMap.h>
#include <Rose/BinaryAnalysis/Disassembler/Base.h>
#include <Rose/BinaryAnalysis/CallingConvention.h>
#include <Sawyer/DistinctList.h>

#include <Rose/Diagnostics.h>
#include <Sawyer/CommandLine.h>

#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <iterator>
#include <iostream>
#include <cassert>

#include "custom-dataflow-engine.h"

using namespace Rose::BinaryAnalysis;
namespace P2 = Rose::BinaryAnalysis::Partitioner2;
namespace Base = Rose::BinaryAnalysis::InstructionSemantics::BaseSemantics;
namespace PartialSymbolic = Rose::BinaryAnalysis::InstructionSemantics::PartialSymbolicSemantics;

using Sawyer::CommandLine::booleanParser;
using Sawyer::CommandLine::anyParser;
using Sawyer::CommandLine::positiveIntegerParser;
using Sawyer::CommandLine::Switch;

/**
 * We build on top of Rose's built-in "Partial Symbolic Semantics" engine, which is able to perform
 * constant propagation and reason about offsets from abstract "terminal" locations (formed when
 * things become too complex for partial symbolic analysis).
 * 
 * Ideally, I'd like to perform slightly deeeper symbolic analysis, for example I think reasoning
 * about dereferencing wouldn't hurt (ie, a grammar that can be loc := terminal(number), loc :=
 * loc+offset, or loc := deref(loc)). If necessary, such analysis should be possible by subclassing
 * (or maybe copy-pasting and then editing) the PartialSymbolicSemantics implementation.
 *
 * General idea: For each procedure, create a number of "static object traces": Methods detected
 * that are called on what we believe to be the same object pointer, in a topological order (eg, if
 * two methods are used in different conditional branches, there's no guarantee of which will appear
 * first in the static trace, but both will appear before anything after the conditional re-joins).
 *
 * The postgame can treat static object traces pretty much the same way as it treats dynamic object
 * traces, except that it does not use the head and tail of the trace to discover constructors and
 * destructors because the constructors and destructors may reside in other procedures.
 * (Note/possible todo: Splitting static traces by destructors isn't really correct if a destructor
 * happens to be called from a conditional branch, because the other branch may appear either before
 * or after it in the topologically sorted static trace, so the split will more or less be nonsense.
 * But I can't think of any cases when a destructor would be called in one side of a conditional but
 * not the other, unless the destructor is being called explicitly).
 *************************************************/

Sawyer::Message::Facility KreoRoseMods::DataFlow::mlog;

namespace Kreo {
    // TODO: PartialSymbolicSemantics uses MemoryCellList here. Why can't we use MemoryCellMap instead? Or can we?
    // TODO: How does MemoryCellMap merging differ when aliasing is turned on or off in the Merger object?

    typedef P2::DataFlow::DfCfg DfCfg;

    // TODO change these to be appropriate for static analysis
    static const char purpose[] =
        "Generates a list of function candidates for a given binary specimen.";
    static const char description[] =
        "This tool disassembles the specified file and generates prints a list of "
        "function candidates to standard output. Functions are printed in decimal, "
        "a single function per line. Pass in --partition-split-thunks if you want "
        "the tool to list true functions and not thunk functions.";

    class Settings {
    public:
        bool enableAliasAnalysis;
        bool enableCallingConventionAnalysis;
        bool enableSymbolProcedureDetection;
        std::string methodCandidatesPath;
        std::string staticTracesPath;
        std::string baseOffsetPath;

        rose_addr_t debugFunctionAddr = 0;
    };

    Settings settings;

    PartialSymbolic::Formatter formatter;

    // See Rose issue #220
    // In order to perform a calling convention analysis simultaneously with the general static analysis, we need to ensure that `RegisterStateGeneric::updateReadProperties` is called when registers are read with side effects enabled, and similarly for `RegisterStateGeneric::updateWriteProperties`, and for some reason this doesn't appear to be the default behavior.
    class RiscOperators : public PartialSymbolic::RiscOperators {
        // Real Constructors //
    public:
        // C++11 syntax for inheriting all constructors:
        using PartialSymbolic::RiscOperators::RiscOperators;

        // Static Allocating Constructors //
    public:
        static Base::RiscOperatorsPtr instanceFromProtoval(const Base::SValuePtr &protoval, const SmtSolver::Ptr &solver = SmtSolverPtr()) {
            return Ptr(new RiscOperators(protoval, solver));
        }
        static Base::RiscOperatorsPtr instanceFromState(const Base::State::Ptr &state, const SmtSolver::Ptr &solver = SmtSolverPtr()) {
            return Ptr(new RiscOperators(state, solver));
        }

        // Virtual constructors
    public:
        virtual Base::RiscOperatorsPtr create(const Base::SValuePtr &protoval,
                                              const SmtSolverPtr &solver = SmtSolverPtr()) const override {
            return instanceFromProtoval(protoval, solver);
        }

        virtual Base::RiscOperatorsPtr create(const Base::State::Ptr &state, const SmtSolverPtr &solver = SmtSolverPtr()) const override {
            return instanceFromState(state, solver);
        }

        // transfer functions
    public:
        virtual Base::SValuePtr readRegister(RegisterDescriptor reg, const Base::SValuePtr &dflt) override {
            Base::SValuePtr retval = PartialSymbolic::RiscOperators::readRegister(reg, dflt);
            PartialSymbolic::RegisterState::promote(currentState()->registerState())->updateReadProperties(reg);
            return retval;
        }

        virtual void writeRegister(RegisterDescriptor reg, const Base::SValuePtr &a) override {
            PartialSymbolic::RiscOperators::writeRegister(reg, a);
            PartialSymbolic::RegisterState::promote(currentState()->registerState())
                ->updateWriteProperties(reg, InstructionSemantics::BaseSemantics::IO_WRITE);
        }

        virtual void writeMemory(RegisterDescriptor segreg,
                                 const Base::SValuePtr &addr,
                                 const Base::SValuePtr &value,
                                 const Base::SValuePtr &condition) override {
            std::cerr << "Write memory: segreg is ";
            if (segreg.isEmpty()) {
                std::cerr << "empty";
            } else {
                Base::SValuePtr segregValue = readRegister(segreg, undefined_(segreg.nBits()));
                segregValue->print(std::cerr, formatter);
                Base::SValuePtr adjustedVa = add(addr, signExtend(segregValue, addr->nBits()));
                std::cerr << ", adjusted addr is ";
                adjustedVa->print(std::cerr, formatter);
            }
            std::cerr << ", addr is ";
            addr->print(std::cerr, formatter);
            std::cerr << " with value ";
            value->print(std::cerr, formatter);
            std::cerr << std::endl;
            PartialSymbolic::RiscOperators::writeMemory(segreg, addr, value, condition);
        }

        virtual Base::SValuePtr fpFromInteger(const Base::SValuePtr &intValue, SgAsmFloatType *fpType) override {
            // TODO there are probably some situations where we could say that the value is preserved, but a floating point isn't going to be an object pointer anyway.
            return undefined_(fpType->get_nBits());
        }
    };

    class StaticObjectTrace {
    public:
        Base::SValue::Ptr objPtr;
        std::vector<P2::Function::Ptr> fns;

        StaticObjectTrace(const Base::SValue::Ptr &objPtr) : objPtr(objPtr) { }
    };

    std::ostream &operator<<(std::ostream &os, const StaticObjectTrace &trace) {
        if (trace.fns.size() <= 1) {
            // A trace with one entry does not convey any information, i.e. is meaningless, so why print it?
            return os;
        }
        for (auto fn : trace.fns) {
            if (fn) {
                os << fn->address();
                if (!fn->name().empty()) {
                    os << " " << fn->name();
                }
                os << std::endl;
            } else {
                // TODO: figure out why this function might be null (indeterminate faked_call??)
                // os << "(unknown)" << std::endl;
            }
        }
        return os;
    }

    class CallingConventionGuess {
    public:
        CallingConventionGuess(RegisterDescriptor thisArgumentRegister, CallingConvention::Definition::Ptr defaultCallingConvention)
            : thisArgumentRegister(thisArgumentRegister), defaultCallingConvention(defaultCallingConvention) { }

        RegisterDescriptor thisArgumentRegister; // register where `this` ptr will be passed to methods.
        CallingConvention::Definition::Ptr defaultCallingConvention; // reasonable default calling convention for non-method procedure calls.
    };

    // Returns either rdi or ecx appropriately. For now we're ignoring 32-bit stdcall, because
    // tracking stack arguments is extra complexity.
    CallingConventionGuess guessCallingConvention(const Disassembler::BasePtr &disassembler) {
        std::string isaName = disassembler->name();
        if (isaName == "i386") {
            return CallingConventionGuess(RegisterDictionary::instancePentium4()->findOrThrow("ecx"),
                                          CallingConvention::Definition::x86_32bit_stdcall());
        }
        assert(isaName == "amd64");
        return CallingConventionGuess(RegisterDictionary::instanceAmd64()->findOrThrow("rdi"),
                                      CallingConvention::Definition::x86_64bit_stdcall());
    }

    // construct an empty/initial state.
    // Note that we pass use this function to create the input both to the initial RiscOperators object and also to the initial state for the start vertex of the dataflow analysis. The state we pass to riscoperators actually doesn't matter, because the dataflow analysis resets it whenever necessary.
    Base::State::Ptr stateFromRegisters(const RegisterDictionary::Ptr &regDict) {
        Base::SValue::Ptr protoval = PartialSymbolic::SValue::instance();
        Base::RegisterState::Ptr registers = PartialSymbolic::RegisterState::instance(protoval, regDict);
        Base::MemoryState::Ptr memory = PartialSymbolic::MemoryState::instance(protoval, protoval);
        memory->byteRestricted(false); // because extracting bytes from a word results in new variables for this domain
        return PartialSymbolic::State::instance(registers, memory);
    }

    enum class LoopRemoverVertexState {
        Unvisited,
        InProgress,
        Complete,
    };

    // Remove loops from a graph, in the style of https://github.com/zhenv5/breaking_cycles_in_noisy_hierarchies/blob/master/remove_cycle_edges_by_dfs.py
    class LoopRemover {
    public:
        LoopRemover(DfCfg *graph) : graph(graph) { }

        void removeLoops(DfCfg::VertexIterator startVertex) {
            assert(getVertexState(startVertex->id()) == LoopRemoverVertexState::Unvisited);
            vertexStates[startVertex->id()] = LoopRemoverVertexState::InProgress;

            DfCfg::EdgeIterator edgeIt = startVertex->outEdges().begin();
            while (edgeIt != startVertex->outEdges().end()) {
                DfCfg::EdgeIterator savedEdgeIt = edgeIt;
                edgeIt++;

                switch (getVertexState(savedEdgeIt->target()->id())) {
                case LoopRemoverVertexState::Unvisited:
                    removeLoops(savedEdgeIt->target());
                    break;
                case LoopRemoverVertexState::InProgress:
                    graph->eraseEdge(savedEdgeIt);
                    break;

                    // else, it has already been visited, we don't need to deal with it.
                }
            }

            vertexStates[startVertex->id()] = LoopRemoverVertexState::Complete;
        }

    private:
        LoopRemoverVertexState getVertexState(size_t id) {
            if (vertexStates.count(id) == 0) {
                return LoopRemoverVertexState::Unvisited;
            } else {
                return vertexStates[id];
            }
        }

        DfCfg *graph;
        std::unordered_map<size_t, LoopRemoverVertexState> vertexStates;
    };

    class AnalyzeProcedureResult {
    public:
        AnalyzeProcedureResult() : success(false), traces(), usesThisPointer(false) { }
        AnalyzeProcedureResult(std::vector<StaticObjectTrace> traces, bool usesThisPointer)
            : traces(traces), usesThisPointer(usesThisPointer), success(true) { }

        std::vector<StaticObjectTrace> traces;
        bool usesThisPointer; // whether the procedure analyzed appears to have the correct calling convention for a method.
        bool success;
    };

    // print all traces, with a blank line between each, and a blank line at the end.
    std::ostream &operator<<(std::ostream &os, const AnalyzeProcedureResult &apr) {
            for (const StaticObjectTrace &trace : apr.traces) {
                os << trace;
            }
            return os;
    }

    AnalyzeProcedureResult analyzeProcedure(const P2::Partitioner &partitioner,
                                            const Disassembler::BasePtr &disassembler,
                                            const P2::Function::Ptr &proc) {
        bool debugFunction = proc->address() == Kreo::settings.debugFunctionAddr;
        if (proc->isThunk()) { // usually dosen't call methods or anything
            return AnalyzeProcedureResult();
        }
        // some of this is modeled off of the dataflow analysis in Rose/BinaryAnalysis/CallingConvention.C

        const CallingConventionGuess cc = guessCallingConvention(disassembler);
        DfCfg dfCfg = P2::DataFlow::buildDfCfg(partitioner, partitioner.cfg(), partitioner.findPlaceholder(proc->address()));
        if (debugFunction) {
            std::string dfCfgDotFileName = "dfcfg-" + proc->name() + ".dot";
            std::cerr << "Printing dot to " << dfCfgDotFileName << std::endl;
            std::ofstream dfCfgDotFile(dfCfgDotFileName);
            P2::DataFlow::dumpDfCfg(dfCfgDotFile, dfCfg);
        }
        // uncomment to see the DfCfg graphs:
        // std::cout << std::endl;
        // dumpDfCfg(std::cout, dfCfg);
        // std::cout << std::endl;

        ///// PREPROCESS GRAPH /////

        size_t startVertexId = 0;
        LoopRemover loopRemover(&dfCfg);
        loopRemover.removeLoops(dfCfg.findVertex(startVertexId));

        if (debugFunction) {
            std::string dfCfgDotFileName = "dfcfg-cut-" + proc->name() + ".dot";
            std::cerr << "Printing cut dot to " << dfCfgDotFileName << std::endl;
            std::ofstream dfCfgDotFile(dfCfgDotFileName);
            P2::DataFlow::dumpDfCfg(dfCfgDotFile, dfCfg);
        }

        ///// FIND TOPOLOGICAL VERTEX ORDERING /////

        // The graph is a DAG after back-edges are removed, so we can loop in topo order
        std::unordered_map<size_t, size_t> vertexNumIncomingEdges;
        std::vector<DfCfg::ConstVertexIterator> readyVertices; // vertices with zero incoming edges
        Sawyer::Container::DistinctList<size_t> topoSortedVertexIds;
        DfCfg::ConstVertexIterator returnVertex = dfCfg.vertices().end();
        for (DfCfg::ConstVertexIterator vertex = dfCfg.vertices().begin();
             vertex != dfCfg.vertices().end();
             vertex++) {
            size_t numIncomingEdges = vertex->nInEdges();
            vertexNumIncomingEdges.emplace(vertex->id(), numIncomingEdges);
            if (numIncomingEdges == 0) {
                readyVertices.push_back(vertex);
            }
        }
        while (!readyVertices.empty()) {
            DfCfg::ConstVertexIterator curVertex = readyVertices.back();
            readyVertices.pop_back();
            topoSortedVertexIds.pushBack(curVertex->id());
            if (curVertex->value().type() == P2::DataFlow::DfCfgVertex::FUNCRET) {
                assert(returnVertex == dfCfg.vertices().end());
                returnVertex = curVertex;
            }

            for (const DfCfg::Edge &outEdge : curVertex->outEdges()) {
                DfCfg::ConstVertexIterator outVertex = outEdge.target();
                const size_t outVertexId = outVertex->id();
                vertexNumIncomingEdges[outVertexId]--;
                if (vertexNumIncomingEdges[outVertexId] == 0) {
                    readyVertices.push_back(outVertex);
                }
            }
        }

        if (returnVertex == dfCfg.vertices().end()) {
            std::cerr << "WARNING: BFS did not reach return vertex!" << std::endl;
            return AnalyzeProcedureResult(); // conceptually could continue to at least output the static object traces, since we only need the return vertex to determine the calling convention, but this indicates that something funky's up with the procedure generally so let's abort.
        }

        
        ///// RUN DATAFLOW /////

        const RegisterDictionary::Ptr regDict = partitioner.instructionProvider().registerDictionary();
        Base::State::Ptr state = stateFromRegisters(regDict);
        Base::RiscOperatorsPtr riscOperators = RiscOperators::instanceFromState(state);
        Base::DispatcherPtr cpu = partitioner.newDispatcher(riscOperators);
        // It appears that creating the dispatcher automatically initializes the state associated with regDict, setting the all-important DS register to zero. It's important to pass this same state into insertStartingVertex below; if you create a new one with stateFromRegisters as I did before, the new state won't be initialized and DS won't be set to zero, causing lots of tractable expressions to be reset to terminals in the static analysis.
        // cpu->initializeState(state);
        P2::DataFlow::TransferFunction transferFn(cpu);
        transferFn.defaultCallingConvention(cc.defaultCallingConvention);
        P2::DataFlow::MergeFunction mergeFn(cpu);
        KreoRoseMods::DataFlow::Engine<DfCfg, Base::State::Ptr, P2::DataFlow::TransferFunction, P2::DataFlow::MergeFunction>
            dfEngine(dfCfg, transferFn, mergeFn);
        dfEngine.name("kreo-static-tracer");

        // dfEngine.reset(State::Ptr()); // TODO what does this do, and why is it CalingConvention.C?
        dfEngine.insertStartingVertex(startVertexId, state);
        dfEngine.worklist(topoSortedVertexIds);
        dfEngine.maxIterations(dfCfg.nVertices()+5); // since we broke loops, should only go once, so this is bascially an assertion.
        try {
            dfEngine.runToFixedPoint(); // should run one iteration per vertex, since no loops.
        } catch (const Base::NotImplemented &e) {
            // TODO: use mlog properly here and elsewhere
            std::cerr << "Not implemented error! " << e.what() << std::endl;
            return AnalyzeProcedureResult();
        } catch (const DataFlow::NotConverging &e) {
            std::cerr << "Dataflow didn't converge! That should never happen, because we reduce to DAG!" << std::endl;
            throw e;
        } catch (const Base::Exception &e) {
            std::cerr << "Generic BaseSemantics::Exception. what(): " << e.what() << std::endl;
            return AnalyzeProcedureResult();
        }
        // catch (const DataFlow::NotConverging &e) {
        //     // TODO: If we re-implement dataflow manually we can avoid this error!
        //     std::cerr << "Data flow analysis didn't converge! " << e.what() << std::endl;
        //     return AnalyzeProcedureResult();
        // }

        ///// ANALYZE DATAFLOW RESULTS FOR STATIC TRACES /////

        // Idea: Take all call vertices in topo order, and put the ones with the same
        // argument into the same trace. While it's possible to find the topo order and
        // build the traces simultaneously, I'll do the topo sort first and then separately
        // loop through just to make the code a bit clearer.


        // Build traces
        std::vector<StaticObjectTrace> traces;
        for (size_t vertexId : topoSortedVertexIds.items()) {
            DfCfg::ConstVertexIterator vertex = dfCfg.findVertex(vertexId);

            if (debugFunction) {
                std::cerr << "==VERTEX " << vertex->id() << "==" << std::endl
                          << "numIncomingEdges = " << vertex->nInEdges() << std::endl
                          << "first argument register @ vertex " << vertex->id() << ": ";
                auto registerState = dfEngine.getInitialState(vertex->id())->registerState();
                registerState->print(std::cerr);
                    // registerState->peekRegister(
                    //     cc.thisArgumentRegister,
                    //     riscOperators->undefined_(cc.thisArgumentRegister.nBits()),
                    //     riscOperators.get()
                    //     )
                    //     ->print(std::cerr, formatter);
                std::cerr << std::endl
                          << "Memory state: " << std::endl;
                dfEngine.getInitialState(vertex->id())->memoryState()->print(std::cerr);
                std::cerr << std::endl << std::endl;
            }

            // Determine whether the current vertex is a call to a function we're interested in, or the start of the graph, which is basically the call of `proc`.
            P2::Function::Ptr vertexProc;
            if (vertex->value().type() == P2::DataFlow::DfCfgVertex::FAKED_CALL) {
                vertexProc = vertex->value().callee();
            } else if (vertex->id() == startVertexId) {
                vertexProc = proc;
            } else {
                continue; // can't find a vertex proc, then this is probably a basic block or sth.
            }

            Base::State::Ptr incomingState = dfEngine.getInitialState(vertex->id());
            // Probably don't need the following check, I'd imaginine there's some way to tell if the register is not fully stored from the peekRegister result?
            if (!PartialSymbolic::RegisterState::promote(incomingState->registerState())->is_wholly_stored(cc.thisArgumentRegister)) {
                continue; // don't know the first argument at this vertex
            }
            Base::SValue::Ptr firstArg = incomingState->registerState()->peekRegister(
                cc.thisArgumentRegister,
                riscOperators->undefined_(cc.thisArgumentRegister.nBits()),
                riscOperators.get()
                );

            // find correct trace to insert into
            StaticObjectTrace *matchingTrace = NULL;
            for (StaticObjectTrace &trace : traces) {
                if (trace.objPtr->mustEqual(firstArg)) {
                    assert(matchingTrace == NULL);
                    matchingTrace = &trace; // I hope this is defined behavior?
                }
            }
            if (matchingTrace == NULL) {
                traces.push_back(StaticObjectTrace(firstArg));
                matchingTrace = &traces.back();
            }
            matchingTrace->fns.push_back(vertexProc);
        }

        ///// ANALYZE DATAFLOW RESULTS FOR CALLING CONVENTION /////
        Base::State::Ptr returnState = dfEngine.getInitialState(returnVertex->id());
        bool usesThisPointer = Base::RegisterStateGeneric::promote(returnState->registerState())->hasPropertyAny(cc.thisArgumentRegister, InstructionSemantics::BaseSemantics::IO_READ_BEFORE_WRITE);

        // All done!
        return AnalyzeProcedureResult(traces, usesThisPointer);
    }
}

int main(int argc, char *argv[]) {
    ROSE_INITIALIZE;

    Rose::Diagnostics::initAndRegister(&KreoRoseMods::DataFlow::mlog, "KreoRoseMods::DataFlow");

    //// COMMAND-LINE PARSING ////

    Sawyer::CommandLine::SwitchGroup kreoSwitchGroup("Kreo Pregame Options");
    kreoSwitchGroup.insert(Switch("enable-alias-analysis")
                           .argument("enable", booleanParser(Kreo::settings.enableAliasAnalysis), "true")
                           .doc("Whether to output static traces."));
    kreoSwitchGroup.insert(Switch("enable-calling-convention-analysis")
                           .argument("enable", booleanParser(Kreo::settings.enableCallingConventionAnalysis), "true")
                           .doc("Whether to try and determine which procedures actually use the \"this\" argument register, to narrow down the method candidate list."));
    kreoSwitchGroup.insert(Switch("enable-symbol-procedure-detection")
                           .argument("enable", booleanParser(Kreo::settings.enableSymbolProcedureDetection), "false")
                           .doc("Whether to \"cheat\" and use debug information/symbols to help detect the procedure list. Desirable if using Kreo in the real world, undesirable when evaluating Kreo's performance on un-stripped binaries."));
    kreoSwitchGroup.insert(Switch("base-offset-path")
                           .argument("path", anyParser(Kreo::settings.baseOffsetPath)));
    kreoSwitchGroup.insert(Switch("method-candidates-path")
                           .argument("path", anyParser(Kreo::settings.methodCandidatesPath)));
    kreoSwitchGroup.insert(Switch("static-traces-path")
                           .argument("path", anyParser(Kreo::settings.staticTracesPath)));
    kreoSwitchGroup.insert(Switch("debug-function")
                           .argument("address", positiveIntegerParser(Kreo::settings.debugFunctionAddr)));

    auto engine = P2::Engine::instance();
    Sawyer::CommandLine::Parser cmdParser = engine->commandLineParser(Kreo::purpose, Kreo::description);
    cmdParser.with(kreoSwitchGroup);
    Sawyer::CommandLine::ParserResult parserResult = cmdParser.parse(argc, argv);
    parserResult.apply(); // loads command-line options from the kreoSwitchGroup into the global `settings` object. Also does whatever the `engine` expects to parse its own command-line options.

    if (Kreo::settings.methodCandidatesPath.empty()) {
        Sawyer::Message::mlog[Sawyer::Message::FATAL] << "No method candidate path specified; see --help" << std::endl;
        exit(EXIT_FAILURE);
    }
    if (Kreo::settings.staticTracesPath.empty()) {
        Sawyer::Message::mlog[Sawyer::Message::FATAL] << "No static traces path specified; see --help" << std::endl;
        exit(EXIT_FAILURE);
    }

    std::vector<std::string> specimen = parserResult.unreachedArgs();

    if (specimen.empty()) {
        Sawyer::Message::mlog[Sawyer::Message::FATAL] << "no binary specimen specified; see --help\n";
        exit(EXIT_FAILURE);
    }

    //// PERFORM ANALYSIS ////

    engine->settings().partitioner.splittingThunks = true;
    engine->settings().partitioner.findingImportFunctions = false; // this is just stuff from other files, right?
    engine->settings().partitioner.findingExportFunctions = Kreo::settings.enableSymbolProcedureDetection;
    engine->settings().partitioner.findingSymbolFunctions = Kreo::settings.enableSymbolProcedureDetection;
    P2::Partitioner partitioner = engine->partition(specimen); // Create and run partitioner

    engine->runPartitionerFinal(partitioner);

    Disassembler::BasePtr disassembler = engine->obtainDisassembler();

    // not sure what the "right" way to get the min address is, so for now we just find the start of the first element in the map.
    // A more robust possibility is to find the segment containing the main function, or to look up a segment by name.
    MemoryMap::Ptr memoryMap = engine->memoryMap();
    // std::cerr << "Dumping memory map" << std::endl;
    // memoryMap->dump(std::cerr);
    size_t baseOffset = memoryMap->nodes().begin()->key().least();
    std::cerr << "Detected minimum address as " << baseOffset << std::endl;
    std::ofstream(Kreo::settings.baseOffsetPath) << baseOffset << std::endl;
    Kreo::settings.debugFunctionAddr += baseOffset;

    std::ofstream methodCandidatesStream(Kreo::settings.methodCandidatesPath);

    std::ofstream staticTracesStream;
    if (Kreo::settings.enableAliasAnalysis) {
        staticTracesStream = std::ofstream(Kreo::settings.staticTracesPath, std::ios_base::out | std::ios_base::binary);
    }

    int numMethodsFound = 0;
    int i = 0;
    for (const P2::Function::Ptr &proc : partitioner.functions()) {
        std::cerr << "Analyze function 0x" << std::hex << proc->address() << ", which is "
                  << std::dec << i++ << " out of " << partitioner.functions().size() << std::endl;
        // there's already a conditional for chunks in the static analysis part, but if static analysis is disabled that won't be reached.
        // We're essentially checking two conditions: 1., before static analysis, that it's not a thunk, and 2., after static analysis, that it uses the this pointer.
        if (proc->isThunk()) {
            continue;
        }

        const size_t relativeProcAddr = proc->address() - baseOffset;
        bool usesThisPointer = true; // assume it uses this pointer, possible set to false if static analysis is enabled and finds that the register is not in fact used.

        if (Kreo::settings.enableAliasAnalysis || Kreo::settings.enableCallingConventionAnalysis) {
            auto analysisResult = Kreo::analyzeProcedure(partitioner, disassembler, proc);

            if (Kreo::settings.enableAliasAnalysis) {
                staticTracesStream << "# Analysis from procedure " << proc->name() << " @ " << relativeProcAddr
                                   << " (" << analysisResult.traces.size() << " many traces):" << std::endl
                                   << analysisResult;
            }

            if (Kreo::settings.enableCallingConventionAnalysis) {
                usesThisPointer = analysisResult.usesThisPointer;
            }
        }

        if (usesThisPointer) {
            numMethodsFound++;
            methodCandidatesStream << relativeProcAddr << std::endl;
        }
    }

    std::cerr << "Final statistics:\n"
              << "    Detected " << numMethodsFound << " methods from " << partitioner.functions().size() << " total procedures."
              << std::endl;
}
