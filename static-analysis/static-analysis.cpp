#include <rose.h>

#include <Rose/BinaryAnalysis/InstructionSemantics/PartialSymbolicSemantics.h>
#include <Rose/BinaryAnalysis/Partitioner2.h>
#include <Rose/BinaryAnalysis/Disassembler.h>

#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
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
    typedef boost::shared_ptr<class RegisterState> RegisterStatePtr;

    // See Rose issue #220
    // In order to perform a calling convention analysis simultaneously with the general static analysis, we need to ensure that `RegisterStateGeneric::updateReadProperties` is called when registers are read with side effects enabled, and for some reason this doesn't appear to be the default behavior.
    class RegisterState : public InstructionSemantics::BaseSemantics::RegisterStateGeneric {
		
        // static allocating constructors
        static RegisterStatePtr instance(const SValuePtr &protoval, const RegisterDictionaryPtr &regDict) {
            return RegisterStatePtr(new RegisterState(protoval, regDict));
        }

        // virtual constructors
        virtual InstructionSemantics::BaseSemantics::RegisterStatePtr create(const SValuePtr &protoval, const RegisterDictionaryPtr &regDict) const override {
            return instance(protoval, regDict);
        }

        virtual InstructionSemantics::BaseSemantics::RegisterStatePtr clone() const override {
            return RegisterStatePtr(new RegisterState(*this));
        }

        // This is the only part that's different than RegisterStateGeneric: update the read properties!
        virtual SValuePtr readRegister(RegisterDescriptor desc, const SValuePtr &dflt, RiscOperators *risc) override {
            RegisterStateGeneric::readRegister(desc, dflt, risc);
            updateReadProperties(desc);
        }
    };

    class StaticObjectTrace {
    public:
        SValue objPtr;
        std::vector<P2::Function::Ptr> fns;

        StaticObjectTrace(const SValue &objPtr) : objPtr(objPtr) { }

        void operator<<(std::ostream &os) const {
            for (auto fn : fns) {
                if (!fn->name().empty()) {
                    os << fn->name() << " ";
                }
                os << fn->address() << std::endl;
            }
        }
    };

    // // we use T directly (no shared pointers or other indirection) because in practice we'll be
    // // storing addresses, which are cheap to copy.
    // template<typename T>
    // class UnionFind {
    // private:
    // 	std::vector<size_t> parents;
    // 	std::unordered_map<T, size_t> valueIndices;
    // public:
    // 	void merge(const T &a, const T &b) {
    // 		std::assert(valueIndices.count(a) != 0 && valueIndices.count(b) != 0);
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

    // Returns either rdi or ecx appropriately. For now we're ignoring 32-bit stdcall, because
    // tracking stack arguments is extra complexity.
    RegisterDescriptor firstArgumentRegister(const Disassembler::BasePtr &assembler) {
        std::string isaName = disassembler->name();
        if (isaName == "i386") {
            return RegisterDictionary::instancePentium4()->findOrThrow("ecx");
        }
        if (isaName == "amd64") {
            return RegisterDictionary::instanceAmd64()->findOrThrow("rdi");
        }

        assert(false); // unsupported architecture
    }

    class AnalyzeProcedureResult {
    public:
        AnalyzeProcedureResult(std::vector<StaticObjectTrace> traces, bool isMethod)
            : traces(traces), isMethod(isMethod) { }

        // print all traces, with a blank line between each, and a blank line at the end.
        void operator<<(const std::ostream &os) {
            for (const StaticObjectTrace &trace : traces) {
                os << trace << std::endl;
            }
        }

        std::vector<StaticObjectTrace> traces;
        bool isMethod; // whether the procedure analyzed appears to have the correct calling convention for a method.
    };

    AnalyzeProcedureResult analyzeProcedure(const P2::Partitioner &partitioner,
                                            const Disassembler::BasePtr &disassembler,
                                            const P2::Function::Ptr &proc) {
        // some of this is modeled off of the dataflow analysis in Rose/BinaryAnalysis/CallingConvention.C

        const RegisterDescriptor firstArgRegister = firstArgumentRegister(disassembler);
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
            return; // conceptually could continue to at least output the static object traces, since we only need the return vertex to determine the calling convention, but this indicates that something funky's up with the procedure generally so let's abort.
        }

        ///// RUN DATAFLOW /////

        // TODO: construct risc operators and so forth
        const RegisterDictionary *regDict = partitioner.instructionProvider().registerDictionary();
        Base::SValuePtr protoval = PartialSymbolic::SValue::instance(); // TODO: is this correct? Wouldn't we actually want each register to be initialized to a newly constructed svalue so that the name counter increases?
        Base::RegisterStatePtr regState = PartialSymbolic::RegisterState::instance(protoval, regDict);
        Base::MemoryStatePtr memState = PartialSymbolic::MemoryState::instance(protoval, protoval);
        Base::StatePtr state = PartialSymbolic::State::instance(regState, memState);
        Base::RiscOperatorsPtr riscOperators = PartialSymbolic::RiscOperators::instance(state, SmtSolver::Ptr()); // no solver
        P2::DataFlow::TransferFunction transferFn(partitioner, cpu);
        transferFn.defaultCallingConvention(TODO);
        P2::DataFlow::MergeFunction merge(cpu);
        P2::DataFlow::Engine dfEngine(dfCfg, transferFn, mergeFn);
        dfEngine.name("kreo-static-tracer");
        transferFn.dfEngineName = dfEngine.name();

        // dfEngine.reset(State::Ptr()); // TODO what does this do, and why is it CalingConvention.C?
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
        for (const DfCfg::Vertex &vertex : dfCfg.vertices()) {
            size_t numIncomingEdges = vertex.inEdges().size();
            vertexNumIncomingEdges.insert(vertex.id(), numIncomingEdges);
            if (numIncomingEdges == 0) {
                readyVertices.push_back(vertex.id());
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

            State::Ptr incomingState = dfEngine.initialState(vertex->id());
            // Probably don't need the following check, I'd imaginine there's some way to tell if the register is not fully stored from the peekRegister result?
            if (!RegisterStateGenericPtr::promote(incomingState->registerState())->is_wholly_stored(firstArgRegister)) {
                continue; // don't know the first argument at this vertex
            }
            SValuePtr firstArg = incomingState->registerState()->peekRegister(
                firstArgRegister,
                SValue::undefined_(),
                riscOperators // TODO
                );

            // find correct trace to insert into
            StaticObjectTrace *matchingTrace = NULL;
            for (StaticObjectTrace &trace : traces) {
                if (trace.objPtr.mustEqual(firstArg)) {
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
        StatePtr returnState = dfEngine.initialState(returnVertex);
        bool isMethod = (!proc->isThunk())
            && returnState->registerState()->hasPropertyAny(firstArgumentRegister, InstructionSemantics::BaseSemantics::IO_READ_BEFORE_WRITE);

        // All done!
        return AnalyzeProcedureResult(traces, isMethod);
    }
}

int main(int argc, char *argv[]) {
    ROSE_INITIALIZE;

    auto engine = P2::Engine::instance();
    // disable cheating. If someone was really using this to reverse engineer a program, they might want to at least enable export functions (I've seen a stripped windows executable that still exported a few functions for some reason).
    engine.settings().partitioner.findingImportFunctions = false;
    engine.settings().partitioner.findingExportFunctions = false;
    engine.settings().partitioner.findingSymbolFunctions = false;

    std::vector<std::string> specimen = engine->parseCommandLine(argc, argv, purpose, description).unreachedArgs();
    if (specimen.empty()) {
        mlog[FATAL] << "no binary specimen specified; see --help\n";
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
