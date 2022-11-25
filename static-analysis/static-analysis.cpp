#include <rose.h>

#include <Rose/BinaryAnalysis/InstructionSemantics/PartialSymbolicSemantics.h>
#include <Rose/BinaryAnalysis/Partitioner2/Partitioner.h>
#include <Rose/BinaryAnalysis/Partitioner2/Function.h>
#include <Rose/BinaryAnalysis/Partitioner2/DataFlow.h>
#include <Rose/BinaryAnalysis/Partitioner2/Engine.h>
#include <Rose/BinaryAnalysis/Disassembler/Base.h>
#include <Rose/BinaryAnalysis/CallingConvention.h>

#include <Rose/Diagnostics.h>
#include <Sawyer/CommandLine.h>

#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <iterator>
#include <iostream>
#include <cassert>

using namespace Rose::BinaryAnalysis;
namespace P2 = Rose::BinaryAnalysis::Partitioner2;
namespace Base = Rose::BinaryAnalysis::InstructionSemantics::BaseSemantics;
namespace PartialSymbolic = Rose::BinaryAnalysis::InstructionSemantics::PartialSymbolicSemantics;

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

    // See Rose issue #220
    // In order to perform a calling convention analysis simultaneously with the general static analysis, we need to ensure that `RegisterStateGeneric::updateReadProperties` is called when registers are read with side effects enabled, and for some reason this doesn't appear to be the default behavior.
    // class RegisterState : public InstructionSemantics::BaseSemantics::RegisterStateGeneric {
		
    //     // static allocating constructors
    //     static RegisterStatePtr instance(const SValuePtr &protoval, const RegisterDictionaryPtr &regDict) {
    //         return RegisterStatePtr(new RegisterState(protoval, regDict));
    //     }

    //     // virtual constructors
    //     virtual InstructionSemantics::BaseSemantics::RegisterStatePtr create(const SValuePtr &protoval, const RegisterDictionaryPtr &regDict) const override {
    //         return instance(protoval, regDict);
    //     }

    //     virtual InstructionSemantics::BaseSemantics::RegisterStatePtr clone() const override {
    //         return RegisterStatePtr(new RegisterState(*this));
    //     }

    //     // This is the only part that's different than RegisterStateGeneric: update the read properties!
    //     virtual SValuePtr readRegister(RegisterDescriptor desc, const SValuePtr &dflt, RiscOperators *risc) override {
    //         RegisterStateGeneric::readRegister(desc, dflt, risc);
    //         updateReadProperties(desc);
    //     }
    // };

    class StaticObjectTrace {
    public:
        Base::SValue::Ptr objPtr;
        std::vector<P2::Function::Ptr> fns;

        StaticObjectTrace(const Base::SValue::Ptr &objPtr) : objPtr(objPtr) { }
    };

    std::ostream &operator<<(std::ostream &os, const StaticObjectTrace &trace) {
            for (auto fn : trace.fns) {
                if (!fn->name().empty()) {
                    os << fn->name() << " ";
                }
                os << fn->address() << std::endl;
            }
            return os;
    }

    // // we use T directly (no shared pointers or other indirection) because in practice we'll be
    // // storing addresses, which are cheap to copy.
    // template<typename T>
    // class UnionFind {
    // private:
    // 	std::vector<size_t> parents;
    // 	std::unordered_map<T, size_t> valueIndices;
    // public:
    // 	void merge(const T &a, const T &b) {
    // 		assert(valueIndices.count(a) != 0 && valueIndices.count(b) != 0);
    // 		parents[valueIndices[b]] = valueIndices[a];
    // 	}

    // 	void add(T a) {
    // 		if (valueIndices.count(a) == 0) {
    // 			size_t newIdx = parents.size();
    // 			parents.push_back(newIdx);
    // 			valueIndices[a] = newIdx;
    // 		}
    // 	}

    // 	std::vector<std::unordered_set<T>> concretize() const {
    // 		std::vector<std::unordered_set<T>> result;

    // 		for (auto pair : valueIndices) {
    // 			if (pair.second > result.size()-1) {
    // 				result.resize(pair.second);
    // 			}
    // 			result[pair.second].add(pair.first);
    // 		}

    // 		return result;
    // 	}
    // };

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

    class AnalyzeProcedureResult {
    public:
        AnalyzeProcedureResult() : success(false), traces(), isMethod(false) { }
        AnalyzeProcedureResult(std::vector<StaticObjectTrace> traces, bool isMethod)
            : traces(traces), isMethod(isMethod), success(true) { }

        std::vector<StaticObjectTrace> traces;
        bool isMethod; // whether the procedure analyzed appears to have the correct calling convention for a method.
        bool success;
    };

    // print all traces, with a blank line between each, and a blank line at the end.
    std::ostream &operator<<(std::ostream &os, const AnalyzeProcedureResult &apr) {
            for (const StaticObjectTrace &trace : apr.traces) {
                os << trace << std::endl;
            }
            return os;
    }

    AnalyzeProcedureResult analyzeProcedure(const P2::Partitioner &partitioner,
                                            const Disassembler::BasePtr &disassembler,
                                            const P2::Function::Ptr &proc) {
        // some of this is modeled off of the dataflow analysis in Rose/BinaryAnalysis/CallingConvention.C

        const CallingConventionGuess cc = guessCallingConvention(disassembler);
        DfCfg dfCfg = P2::DataFlow::buildDfCfg(partitioner, partitioner.cfg(), partitioner.findPlaceholder(proc->address()));

        ///// PREPROCESS GRAPH /////

        // Perform BFS over the graph, removing back-edges.
        std::unordered_set<size_t> exploredVertexIds;
        std::deque<DfCfg::VertexIterator> vertexQueue;

        size_t startVertexId = 0;
        vertexQueue.push_back(dfCfg.findVertex(startVertexId));
        DfCfg::ConstVertexIterator returnVertex = dfCfg.vertices().end();

        while (!vertexQueue.empty()) {
            DfCfg::VertexIterator curVertex = vertexQueue.front();
            vertexQueue.pop_front();

            if (exploredVertexIds.count(curVertex->id()) != 0) {
                continue;
            }

            DfCfg::EdgeIterator edgeIt = curVertex->outEdges().begin();
            while (edgeIt != curVertex->outEdges().end()) {
                // save the edge iterator because you can't increment an edge after erasing it.
                DfCfg::EdgeIterator savedEdgeIt = edgeIt;
                edgeIt++;
                // remove back edges, to eliminate loops
                if (exploredVertexIds.count(savedEdgeIt->target()->id()) != 0) {
                    dfCfg.eraseEdge(savedEdgeIt);
                }
            }

            if (curVertex->value().type() == P2::DataFlow::DfCfgVertex::FUNCRET) {
                assert(returnVertex == dfCfg.vertices().end());
                returnVertex = curVertex;
            }
        }

        if (returnVertex == dfCfg.vertices().end()) {
            std::cerr << "WARNING: BFS did not reach return vertex!" << std::endl;
            return AnalyzeProcedureResult(); // conceptually could continue to at least output the static object traces, since we only need the return vertex to determine the calling convention, but this indicates that something funky's up with the procedure generally so let's abort.
        }

        ///// RUN DATAFLOW /////

        const RegisterDictionary::Ptr regDict = partitioner.instructionProvider().registerDictionary();
        Base::SValue::Ptr protoval = PartialSymbolic::SValue::instance(); // TODO: is this correct? Wouldn't we actually want each register to be initialized to a newly constructed svalue so that the name counter increases?
        Base::RiscOperatorsPtr riscOperators = PartialSymbolic::RiscOperators::instanceFromRegisters(regDict); // no solver
        Base::DispatcherPtr cpu = partitioner.newDispatcher(riscOperators);
        P2::DataFlow::TransferFunction transferFn(cpu);
        transferFn.defaultCallingConvention(cc.defaultCallingConvention);
        P2::DataFlow::MergeFunction mergeFn(cpu);
        P2::DataFlow::Engine dfEngine(dfCfg, transferFn, mergeFn);
        dfEngine.name("kreo-static-tracer");

        // dfEngine.reset(State::Ptr()); // TODO what does this do, and why is it CalingConvention.C?
        Base::RegisterStatePtr initialRegState = PartialSymbolic::RegisterState::instance(protoval, regDict);
        Base::MemoryStatePtr initialMemState = PartialSymbolic::MemoryState::instance(protoval, protoval);
        Base::StatePtr initialState = PartialSymbolic::State::instance(initialRegState, initialMemState);
        dfEngine.insertStartingVertex(startVertexId, initialState);
        dfEngine.runToFixedPoint(); // should run one iteration per vertex, since no loops.

        ///// ANALYZE DATAFLOW RESULTS FOR STATIC TRACES /////

        // Idea: Take all call vertices in topo order, and put the ones with the same
        // argument into the same trace. While it's possible to find the topo order and
        // build the traces simultaneously, I'll do the topo sort first and then separately
        // loop through just to make the code a bit clearer.

        // The graph is a DAG after back-edges are removed, so we can loop in topo order
        std::unordered_map<size_t, size_t> vertexNumIncomingEdges;
        std::vector<DfCfg::ConstVertexIterator> readyVertices; // vertices with zero incoming edges
        std::vector<DfCfg::ConstVertexIterator> topoSortedVertices;
        for (DfCfg::ConstVertexIterator vertex = dfCfg.vertices().begin();
             vertex != dfCfg.vertices().end();
             vertex++) {
            // inEdges underlying iterator is not random access, so we cannot use any .size()
            auto inEdges = vertex->inEdges();
            // std::distance doesn't seem to work here for some reason -- probably some disconnect between Boost iterators and std iterators
            size_t numIncomingEdges = std::distance(inEdges.begin(), inEdges.end());
            vertexNumIncomingEdges.emplace(vertex->id(), numIncomingEdges);
            if (numIncomingEdges == 0) {
                readyVertices.push_back(vertex);
            }
        }
        while (!readyVertices.empty()) {
            DfCfg::ConstVertexIterator curVertex = readyVertices.back();
            readyVertices.pop_back();
            topoSortedVertices.push_back(curVertex);

            for (const DfCfg::Edge &outEdge : curVertex->outEdges()) {
                DfCfg::ConstVertexIterator outVertex = outEdge.target();
                const size_t outVertexId = outVertex->id();
                vertexNumIncomingEdges[outVertexId]--;
                if (vertexNumIncomingEdges[outVertexId] == 0) {
                    readyVertices.push_back(outVertex);
                }
            }
        }

        // Build traces
        std::vector<StaticObjectTrace> traces;
        for (DfCfg::ConstVertexIterator vertex : topoSortedVertices) {
            // only interested in calls
            if (vertex->value().type() != P2::DataFlow::DfCfgVertex::FAKED_CALL) {
                continue;
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
            matchingTrace->fns.push_back(vertex->value().callee());
        }

        ///// ANALYZE DATAFLOW RESULTS FOR CALLING CONVENTION /////
        Base::State::Ptr returnState = dfEngine.getInitialState(returnVertex->id());
        bool isMethod = (!proc->isThunk())
            && Base::RegisterStateGeneric::promote(returnState->registerState())->hasPropertyAny(cc.thisArgumentRegister, InstructionSemantics::BaseSemantics::IO_READ_BEFORE_WRITE);

        // All done!
        return AnalyzeProcedureResult(traces, isMethod);
    }
}

int main(int argc, char *argv[]) {
    ROSE_INITIALIZE;

    auto engine = P2::Engine::instance();
    // disable cheating. If someone was really using this to reverse engineer a program, they might want to at least enable export functions (I've seen a stripped windows executable that still exported a few functions for some reason).
    engine->settings().partitioner.findingImportFunctions = false;
    engine->settings().partitioner.findingExportFunctions = false;
    engine->settings().partitioner.findingSymbolFunctions = false;

    std::vector<std::string> specimen = engine->parseCommandLine(argc, argv, Kreo::purpose, Kreo::description).unreachedArgs();
    if (specimen.empty()) {
        Sawyer::Message::mlog[Sawyer::Message::FATAL] << "no binary specimen specified; see --help\n";
        exit(EXIT_FAILURE);
    }

    P2::Partitioner partitioner = engine->partition(specimen); // Create and run partitioner
    engine->runPartitionerFinal(partitioner);

    Disassembler::BasePtr disassembler = engine->obtainDisassembler();

    // TODO: change?
    const size_t kMinAddress = 0x400000;

    for (const P2::Function::Ptr &proc : partitioner.functions()) {
        auto analysisResult = Kreo::analyzeProcedure(partitioner, disassembler, proc);
        const size_t relativeProcAddr = proc->address() - kMinAddress;

        std::cout << "# Analysis from procedure " << proc->name() << " @ " << proc->address()
                  << " (" << analysisResult.traces.size() << " many traces):" << std::endl
                  << analysisResult;

        if (analysisResult.isMethod) {
            // TODO: output method list and static object traces to different files.
        }
    }
}
