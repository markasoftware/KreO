#include <rose.h>

//

#include <Rose/BinaryAnalysis/CallingConvention.h>
#include <Rose/BinaryAnalysis/Disassembler/Base.h>
#include <Rose/BinaryAnalysis/InstructionSemantics/PartialSymbolicSemantics.h>
#include <Rose/BinaryAnalysis/MemoryMap.h>
#include <Rose/BinaryAnalysis/Partitioner2/BasicBlock.h>
#include <Rose/BinaryAnalysis/Partitioner2/DataFlow.h>
#include <Rose/BinaryAnalysis/Partitioner2/Engine.h>
#include <Rose/BinaryAnalysis/Partitioner2/Function.h>
#include <Rose/BinaryAnalysis/Partitioner2/Partitioner.h>
#include <Rose/BinaryAnalysis/RegisterNames.h>
#include <Rose/BinaryAnalysis/RegisterParts.h>
#include <Rose/Diagnostics.h>
#include <Sawyer/CommandLine.h>
#include <Sawyer/DistinctList.h>

#include <algorithm>
#include <cassert>
#include <deque>
#include <iostream>
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "custom_dataflow_engine.h"

Sawyer::Message::Facility KreoRoseMods::DataFlow::mlog;

namespace P2 = Rose::BinaryAnalysis::Partitioner2;
namespace Instruction = Rose::BinaryAnalysis::InstructionSemantics;
namespace Base = Rose::BinaryAnalysis::InstructionSemantics::BaseSemantics;
namespace PartialSymbolic = Rose::BinaryAnalysis::InstructionSemantics::PartialSymbolicSemantics;
namespace Symbolic = Rose::BinaryAnalysis::InstructionSemantics::SymbolicSemantics;

using Sawyer::CommandLine::anyParser;
using Sawyer::CommandLine::booleanParser;
using Sawyer::CommandLine::positiveIntegerParser;
using Sawyer::CommandLine::Switch;
using Sawyer::Message::DEBUG;
using Sawyer::Message::ERROR;
using Sawyer::Message::mlog;

/**
 * We build on top of Rose's built-in "Partial Symbolic Semantics" engine, which
 * is able to perform constant propagation and reason about offsets from abstract
 * "terminal" locations (formed when things become too complex for partial
 * symbolic analysis).
 *
 * Ideally, I'd like to perform slightly deeeper symbolic analysis, for example
 * I think reasoning about dereferencing wouldn't hurt (ie, a grammar that can be
 * loc := terminal(number), loc := loc+offset, or loc := deref(loc)). If
 * necessary, such analysis should be possible by subclassing (or maybe
 * copy-pasting and then editing) the PartialSymbolicSemantics implementation.
 *
 * General idea: For each procedure, create a number of "static object traces":
 * Methods detected that are called on what we believe to be the same object
 * pointer, in a topological order (eg, if two methods are used in different
 * conditional branches, there's no guarantee of which will appear first in the
 * static trace, but both will appear before anything after the conditional
 * re-joins).
 *
 * The postgame can treat static object traces pretty much the same way as it
 * treats dynamic object traces, except that it does not use the head and tail of
 * the trace to discover constructors and destructors because the constructors
 * and destructors may reside in other procedures. (Note/possible todo: Splitting
 * static traces by destructors isn't really correct if a destructor happens to
 * be called from a conditional branch, because the other branch may appear
 * either before or after it in the topologically sorted static trace, so the
 * split will more or less be nonsense. But I can't think of any cases when a
 * destructor would be called in one side of a conditional but not the other,
 * unless the destructor is being called explicitly).
 */

namespace Kreo {
// TODO: PartialSymbolicSemantics uses MemoryCellList here. Why can't we use
// MemoryCellMap instead? Or can we?
// TODO: How does MemoryCellMap merging differ when aliasing is turned on or off
// in the Merger object?

using DfCfg = P2::DataFlow::DfCfg;
using VertexIdSet = Sawyer::Container::DistinctList<size_t>;

// TODO change these to be appropriate for static analysis
static const char purpose[] = "Performs the static analysis component of KreO.";
static const char description[] =
    "Generates a list of function candidates for a given binary. Also generates a list of static object-traces when "
    "requested";

class Settings {
 public:
  bool enable_static_alias_analysis{true};
  bool enable_calling_convention_analysis{true};
  bool enableSymbolProcedureDetection{true};
  std::string methodCandidatesPath{};
  std::string staticTracesPath{};
  std::string baseOffsetPath{};

  rose_addr_t debugFunctionAddr{0};
};

Settings settings;

// See Rose issue #220
// In order to perform a calling convention analysis simultaneously with the
// general static analysis, we need to ensure that
// `RegisterStateGeneric::updateReadProperties` is called when registers are
// read with side effects enabled, and similarly for
// `RegisterStateGeneric::updateWriteProperties`, and for some reason this
// doesn't appear to be the default behavior.
class RiscOperators : public PartialSymbolic::RiscOperators {
  // Real Constructors //
 public:
  // C++11 syntax for inheriting all constructors:
  using PartialSymbolic::RiscOperators::RiscOperators;

  // Static Allocating Constructors //
 public:
  static Base::RiscOperatorsPtr instanceFromProtoval(
      const Base::SValuePtr &protoval,
      const Rose::BinaryAnalysis::SmtSolverPtr &solver = Rose::BinaryAnalysis::SmtSolverPtr()) {
    return Ptr(new RiscOperators(protoval, solver));
  }
  static Base::RiscOperatorsPtr instanceFromState(
      const Base::StatePtr &state,
      const Rose::BinaryAnalysis::SmtSolverPtr &solver = Rose::BinaryAnalysis::SmtSolverPtr()) {
    return Ptr(new RiscOperators(state, solver));
  }

  // Virtual constructors
 public:
  virtual Base::RiscOperatorsPtr create(
      const Base::SValuePtr &protoval,
      const Rose::BinaryAnalysis::SmtSolverPtr &solver = Rose::BinaryAnalysis::SmtSolverPtr()) const override {
    return instanceFromProtoval(protoval, solver);
  }

  virtual Base::RiscOperatorsPtr create(
      const Base::StatePtr &state,
      const Rose::BinaryAnalysis::SmtSolverPtr &solver = Rose::BinaryAnalysis::SmtSolverPtr()) const override {
    return instanceFromState(state, solver);
  }

  // transfer functions
 public:
  // virtual Base::SValuePtr readRegister(Rose::BinaryAnalysis::RegisterDescriptor reg,
  //                                      const Base::SValuePtr &dflt) override {
  //   Base::SValuePtr retval = PartialSymbolic::RiscOperators::readRegister(reg, dflt);
  //   PartialSymbolic::RegisterState::promote(currentState()->registerState())->updateReadProperties(reg);
  //   return retval;
  // }

  // virtual void writeRegister(Rose::BinaryAnalysis::RegisterDescriptor reg, const Base::SValuePtr &a) override {
  //   PartialSymbolic::RiscOperators::writeRegister(reg, a);
  //   mlog[DEBUG] << "Write to register: " << reg.toString() << " value " << a->toString() << std::endl;
  //   PartialSymbolic::RegisterState::promote(currentState()->registerState())
  //       ->updateWriteProperties(reg, Rose::BinaryAnalysis::InstructionSemantics::BaseSemantics::IO_WRITE);
  // }

  // virtual void writeMemory(Rose::BinaryAnalysis::RegisterDescriptor segreg, const Base::SValuePtr &addr,
  //                          const Base::SValuePtr &value, const Base::SValuePtr &condition) override {
  //   mlog[DEBUG] << "Write memory: segreg is ";
  //   if (segreg.isEmpty()) {
  //     mlog[DEBUG] << "empty";
  //   } else {
  //     Base::SValuePtr segregValue = readRegister(segreg, undefined_(segreg.nBits()));
  //     segregValue->print(mlog[DEBUG], formatter);
  //     Base::SValuePtr adjustedVa = add(addr, signExtend(segregValue, addr->nBits()));
  //     mlog[DEBUG] << ", adjusted addr is ";
  //     adjustedVa->print(mlog[DEBUG], formatter);
  //   }
  //   mlog[DEBUG] << ", addr is ";
  //   addr->print(mlog[DEBUG], formatter);
  //   mlog[DEBUG] << " with value ";
  //   value->print(mlog[DEBUG], formatter);
  //   mlog[DEBUG] << std::endl;
  //   PartialSymbolic::RiscOperators::writeMemory(segreg, addr, value, condition);
  // }

  virtual Base::SValuePtr fpFromInteger(const Base::SValuePtr &intValue, SgAsmFloatType *fpType) override {
    // TODO there are probably some situations where we could say that the value
    // is preserved, but a floating point isn't going to be an object pointer
    // anyway.
    return undefined_(fpType->get_nBits());
  }

  //  private:
  //   PartialSymbolic::Formatter formatter;
};

class StaticObjectTrace {
 public:
  Base::SValuePtr objPtr;
  std::vector<P2::FunctionPtr> fns;
  rose_addr_t baseOffset;

  StaticObjectTrace(const Base::SValuePtr &objPtr, rose_addr_t baseOffset) : objPtr(objPtr), baseOffset(baseOffset) {}
};

std::ostream &operator<<(std::ostream &os, const StaticObjectTrace &trace) {
  if (trace.fns.size() <= 1) {
    // A trace with one entry does not convey any information, i.e. is
    // meaningless, so why print it?
    return os;
  }
  for (auto fn : trace.fns) {
    if (fn) {
      rose_addr_t relAddr = fn->address() - trace.baseOffset;
      os << std::hex << relAddr;
      if (!fn->name().empty()) {
        os << " " << fn->name();
      }
      os << '\n';
    } else {
      // TODO: figure out why this function might be null (indeterminate faked_call??)
      // os << "(unknown)" << std::endl;
    }
  }
  os << std::endl;
  return os;
}

// ============================================================================
struct CallingConventionGuess {
  CallingConventionGuess(Rose::BinaryAnalysis::RegisterDescriptor thisArgumentRegister,
                         Rose::BinaryAnalysis::CallingConvention::DefinitionPtr defaultCallingConvention)
      : thisArgumentRegister(thisArgumentRegister),
        defaultCallingConvention(defaultCallingConvention) {}

  // register where `this` ptr will be passed to methods.
  Rose::BinaryAnalysis::RegisterDescriptor thisArgumentRegister;

  // reasonable default calling convention for non-method procedure calls.
  Rose::BinaryAnalysis::CallingConvention::DefinitionPtr defaultCallingConvention;
};

// ============================================================================
// Returns either rdi or ecx appropriately. For now we're ignoring 32-bit
// stdcall, because tracking stack arguments is extra complexity.
CallingConventionGuess guessCallingConvention(const Rose::BinaryAnalysis::Disassembler::BasePtr &disassembler) {
  const std::string &isaName(disassembler->name());
  if (isaName == "i386") {
    return CallingConventionGuess(Rose::BinaryAnalysis::RegisterDictionary::instancePentium4()->findOrThrow("ecx"),
                                  Rose::BinaryAnalysis::CallingConvention::Definition::x86_32bit_stdcall());
  }
  assert(isaName == "amd64");
  return CallingConventionGuess(Rose::BinaryAnalysis::RegisterDictionary::instanceAmd64()->findOrThrow("rdi"),
                                Rose::BinaryAnalysis::CallingConvention::Definition::x86_64bit_stdcall());
}

// ============================================================================
// construct an empty/initial state.
// Note that we pass use this function to create the input both to the initial
// RiscOperators object and also to the initial state for the start vertex of
// the dataflow analysis. The state we pass to riscoperators actually doesn't
// matter, because the dataflow analysis resets it whenever necessary.
Base::StatePtr stateFromRegisters(const Rose::BinaryAnalysis::RegisterDictionaryPtr &regDict) {
  Base::SValuePtr protoval(PartialSymbolic::SValue::instance());
  Base::RegisterStatePtr registers = PartialSymbolic::RegisterState::instance(protoval, regDict);
  Base::MemoryStatePtr memory = PartialSymbolic::MemoryState::instance(protoval, protoval);
  memory->byteRestricted(false);  // because extracting bytes from a word results
                                  // in new variables for this domain
  return PartialSymbolic::State::instance(registers, memory);
}

enum class LoopRemoverVertexState {
  Unvisited,
  InProgress,
  Complete,
};

// Remove loops from a graph, in the style of
// https://github.com/zhenv5/breaking_cycles_in_noisy_hierarchies/blob/master/remove_cycle_edges_by_dfs.py
class LoopRemover {
 public:
  LoopRemover(DfCfg *graph) : graph(graph) {}

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

// ============================================================================
class AnalyzeProcedureResult {
 public:
  AnalyzeProcedureResult() : success(false), traces(), usesThisPointer(false) {}
  AnalyzeProcedureResult(std::vector<StaticObjectTrace> traces, bool usesThisPointer)
      : traces(traces),
        usesThisPointer(usesThisPointer),
        success(true) {}

  size_t numMeaningfulTraces() const {
    size_t meaningfulTraces{};

    for (auto &trace : traces) {
      if (trace.fns.size() > 1) {
        meaningfulTraces++;
      }
    }

    return meaningfulTraces;
  }

  std::vector<StaticObjectTrace> traces;
  bool usesThisPointer;  // whether the procedure analyzed appears to have the correct calling convention for a method.
  bool success;
};

// ============================================================================
// print all traces, with a blank line between each, and a blank line at the
// end.
std::ostream &operator<<(std::ostream &os, const AnalyzeProcedureResult &apr) {
  for (const StaticObjectTrace &trace : apr.traces) {
    os << trace;
  }
  return os;
}

// ============================================================================
std::tuple<VertexIdSet, DfCfg::ConstVertexIterator> findTopologicalVertexOrdering(const DfCfg &dfCfg) {
  // The graph is a DAG after back-edges are removed, so we can loop in
  // topological order Graph mappnig vertex to the number of incoming edges
  std::unordered_map<size_t, size_t> vertexNumIncomingEdges;

  // Frontier vertices, those being explored next. Initially build by looking through the cfg and
  // finding vertices that have 0 incoming edges.
  std::vector<DfCfg::ConstVertexIterator> frontierVertices;

  for (DfCfg::ConstVertexIterator vertex(dfCfg.vertices().begin()); vertex != dfCfg.vertices().end(); vertex++) {
    size_t numIncomingEdges(vertex->nInEdges());

    vertexNumIncomingEdges[vertex->id()] = numIncomingEdges;

    // Initially build the frontier with vertices that do not have any incoming
    // edges.
    if (numIncomingEdges == 0) {
      frontierVertices.push_back(vertex);
    }
  }

  // Vertex indices, sorted in topological order.
  VertexIdSet topoSortedVertices;

  DfCfg::ConstVertexIterator retVertex(dfCfg.vertices().end());

  while (!frontierVertices.empty()) {
    DfCfg::ConstVertexIterator curVertex(frontierVertices.back());
    frontierVertices.pop_back();

    topoSortedVertices.pushBack(curVertex->id());

    if (curVertex->value().type() == P2::DataFlow::DfCfgVertex::FUNCRET) {
      // There should only be one return vertex
      assert(retVertex == dfCfg.vertices().end());
      retVertex = curVertex;
    }

    for (const DfCfg::Edge &outEdge : curVertex->outEdges()) {
      DfCfg::ConstVertexIterator outVertex(outEdge.target());

      const size_t outVertexId(outVertex->id());

      // We have explored an edge leading to the given vertex.
      vertexNumIncomingEdges[outVertexId]--;

      // We have explored all possible paths to the vertex, so add to frontier
      // such that we can explore all paths leading from the vertex.
      if (vertexNumIncomingEdges[outVertexId] == 0) {
        frontierVertices.push_back(outVertex);
      }
    }
  }

  if (retVertex == dfCfg.vertices().end()) {
    assert(false);
    mlog[ERROR] << "BFS did not reach return vertex!" << std::endl;
    // conceptually could continue to at least output the static object traces, since we only need the return vertex to
    // determine the calling convention, but this indicates that something funky's
    // up with the procedure generally so let's abort.
    throw std::runtime_error("");
  }

  return std::make_tuple(topoSortedVertices, retVertex);
}

// ============================================================================
void updateInputRegisters(const Base::StatePtr &finalState, Rose::BinaryAnalysis::RegisterParts &inputRegisters) {
  inputRegisters.clear();

  auto regs(Base::RegisterStateGeneric::promote(finalState->registerState()));

  for (Rose::BinaryAnalysis::RegisterDescriptor reg : regs->findProperties(Base::IO_READ_BEFORE_WRITE)) {
    inputRegisters.insert(reg);
  }
}

// ============================================================================
void initializeCalleeSavedLocations(Base::StatePtr state, Base::RiscOperatorsPtr ops,
                                    const CallingConventionGuess &cc) {
  // just read ecx/rdi to put it into registerstate
  state->registerState()->readRegister(
      cc.thisArgumentRegister, ops->undefined_(cc.thisArgumentRegister.nBits()), ops.get());
}

// ============================================================================
AnalyzeProcedureResult analyzeProcedure(const P2::PartitionerPtr &partitioner,
                                        const Rose::BinaryAnalysis::Disassembler::BasePtr &disassembler,
                                        const P2::FunctionPtr &proc, rose_addr_t baseOffset) {
  rose_addr_t absAddr(proc->address());
  rose_addr_t relAddr(absAddr - baseOffset);
  bool debugFunction(relAddr == Kreo::settings.debugFunctionAddr);

  if (proc->isThunk()) {  // usually doesn't call methods or anything
    return AnalyzeProcedureResult();
  }

  // some of this is modeled off of the dataflow analysis in
  // Rose/BinaryAnalysis/CallingConvention.C

  const CallingConventionGuess cc(guessCallingConvention(disassembler));
  DfCfg dfCfg(P2::DataFlow::buildDfCfg(partitioner, partitioner->cfg(), partitioner->findPlaceholder(absAddr)));

  if (debugFunction) {
    mlog[DEBUG] << "Writing original DF CFG to file" << std::endl;
    std::string dfCfgDotFileName = "dfcfg-" + proc->name() + ".dot";
    std::ofstream dfCfgDotFile(dfCfgDotFileName);
    P2::DataFlow::dumpDfCfg(dfCfgDotFile, dfCfg);
  }

  // ==========================================================================
  // PREPROCESS GRAPH

  size_t startVertexId(0);
  LoopRemover loopRemover(&dfCfg);
  loopRemover.removeLoops(dfCfg.findVertex(startVertexId));

  if (debugFunction) {
    mlog[DEBUG] << "Writing DF CFG with loops removed to file" << std::endl;
    std::string dfCfgDotFileName = "dfcfg-cut-" + proc->name() + ".dot";
    std::ofstream dfCfgDotFile(dfCfgDotFileName);
    P2::DataFlow::dumpDfCfg(dfCfgDotFile, dfCfg);
  }

  // ==========================================================================
  // FIND TOPOLOGICAL VERTEX ORDERING

  std::tuple<VertexIdSet, DfCfg::ConstVertexIterator> ret;
  try {
    ret = findTopologicalVertexOrdering(dfCfg);
  } catch (std::runtime_error) {
    return AnalyzeProcedureResult();
  }

  VertexIdSet topoSortedVertices(std::get<0>(ret));
  DfCfg::ConstVertexIterator retVertex(std::get<1>(ret));

  // ==========================================================================
  // RUN DATAFLOW

  const Rose::BinaryAnalysis::RegisterDictionaryPtr regDict(partitioner->instructionProvider().registerDictionary());

  Base::StatePtr initialState(stateFromRegisters(regDict));

  Base::RiscOperatorsPtr riscOperators(RiscOperators::instanceFromState(initialState));

  Base::DispatcherPtr cpu(partitioner->newDispatcher(riscOperators));

  // It appears that creating the dispatcher automatically initializes the state
  // associated with regDict, setting the all-important DS register to zero.
  // It's important to pass this same state into insertStartingVertex below; if
  // you create a new one with stateFromRegisters as I did before, the new state
  // won't be initialized and DS won't be set to zero, causing lots of tractable
  // expressions to be reset to terminals in the static analysis.

  // ensures that ecx/rdi is initialized before analysis begins,
  // because they do have a value before function call (at least in the
  // conventions we're interested in). Lots of room for improvement
  // here, ask mark, I have a document with a more detailed todo about
  // this and doing it for other callee-saved locations more robustly.
  initializeCalleeSavedLocations(initialState, riscOperators, cc);

  cpu->initializeState(initialState);
  P2::DataFlow::TransferFunction xfer(cpu);
  xfer.defaultCallingConvention(cc.defaultCallingConvention);
  P2::DataFlow::MergeFunction mergeFn(cpu);

  using DfEngine = KreoRoseMods::DataFlow::
      Engine<DfCfg, Base::StatePtr, P2::DataFlow::TransferFunction, P2::DataFlow::MergeFunction>;

  DfEngine dfEngine(dfCfg, xfer, mergeFn);
  dfEngine.name("kreo-static-tracer");

  // since we broke loops, should only go once, so this is basically an
  // assertion.
  dfEngine.maxIterations(dfCfg.nVertices() + 5);
  dfEngine.reset(Base::StatePtr());
  dfEngine.insertStartingVertex(startVertexId, initialState);
  dfEngine.worklist(topoSortedVertices);

  try {
    // should run one iteration per vertex, since no loops.
    dfEngine.runToFixedPoint();
  } catch (const Base::NotImplemented &e) {
    mlog[ERROR] << "Not implemented error! what(): " << e.what() << std::endl;
    return AnalyzeProcedureResult();
  } catch (const Rose::BinaryAnalysis::DataFlow::NotConverging &e) {
    mlog[ERROR] << "Dataflow didn't converge! That should never happen, because "
                   "we reduce to DAG!"
                << std::endl;
    throw e;
  } catch (const Base::Exception &e) {
    mlog[ERROR] << "Generic BaseSemantics::Exception. what(): " << e.what() << std::endl;
    return AnalyzeProcedureResult();
  }

  // =============================================================================================
  // ANALYZE DATAFLOW RESULTS FOR STATIC TRACES
  // Idea: Take all call vertices in topo order, and put the ones with the same
  // argument into the same trace. While it's possible to find the topo order
  // and build the traces simultaneously, I'll do the topo sort first and then
  // separately loop through just to make the code a bit clearer.

  // Build traces
  std::vector<StaticObjectTrace> traces;
  for (size_t vertexId : topoSortedVertices.items()) {
    DfCfg::ConstVertexIterator vertex(dfCfg.findVertex(vertexId));

    if (debugFunction) {
      mlog[DEBUG] << "==VERTEX " << vertex->id() << "==" << std::endl
                  << "numIncomingEdges = " << vertex->nInEdges() << std::endl
                  << "Register state @ vertex " << vertex->id() << ":" << std::endl;

      dfEngine.getInitialState(vertex->id())->registerState()->print(mlog[DEBUG]);

      mlog[DEBUG] << "Memory state:" << std::endl;
      dfEngine.getInitialState(vertex->id())->memoryState()->print(mlog[DEBUG]);
      mlog[DEBUG] << std::endl << std::endl;
    }

    // Determine whether the current vertex is a call to a function we're
    // interested in, or the start of the graph, which is basically the call
    // of `proc`.
    P2::FunctionPtr vertexProc;
    if (vertex->value().type() == P2::DataFlow::DfCfgVertex::FAKED_CALL) {
      vertexProc = vertex->value().callee();
    } else if (vertex->id() == startVertexId) {
      vertexProc = proc;
    } else {
      continue;  // can't find a vertex proc, then this is probably a basic block or sth.
    }

    // Get vertex register state to check the register of interest (the this pointer).
    Base::StatePtr initialState(dfEngine.getInitialState(vertex->id()));

    // Probably don't need the following check, I'd imagine there's some way
    // to tell if the register is not fully stored from the peekRegister result?
    if (!PartialSymbolic::RegisterState::promote(initialState->registerState())
             ->is_wholly_stored(cc.thisArgumentRegister)) {
      continue;  // don't know the first argument at this vertex
    }

    auto undefined(riscOperators->undefined_(cc.thisArgumentRegister.nBits()));

    Base::SValuePtr firstArg(
        initialState->registerState()->peekRegister(cc.thisArgumentRegister, undefined, riscOperators.get()));

    assert(undefined != firstArg);  // TODO remove

    // find correct trace to insert into
    StaticObjectTrace *matchingTrace = NULL;
    for (StaticObjectTrace &trace : traces) {
      if (trace.objPtr->mustEqual(firstArg)) {
        assert(matchingTrace == NULL);
        matchingTrace = &trace;
      }
    }
    // create a new trace for the newly discovered this-pointer
    if (matchingTrace == NULL) {
      traces.push_back(StaticObjectTrace(firstArg, baseOffset));
      matchingTrace = &traces.back();
    }
    matchingTrace->fns.push_back(vertexProc);
  }

  // =============================================================================================
  // ANALYZE DATAFLOW RESULTS FOR CALLING CONVENTION

  auto finalState(dfEngine.getInitialState(retVertex->id()));

  Rose::BinaryAnalysis::RegisterParts inputRegisters;

  updateInputRegisters(finalState, inputRegisters);

  bool usesThisPointer(inputRegisters.existsAll(cc.thisArgumentRegister));

  // All done!
  return AnalyzeProcedureResult(traces, usesThisPointer);
}
}  // namespace Kreo

int main(int argc, char *argv[]) {
  ROSE_INITIALIZE;

  Rose::Diagnostics::initAndRegister(&KreoRoseMods::DataFlow::mlog, "KreoRoseMods::DataFlow");

  // =============================================================================================
  // COMMAND-LINE PARSING

  Sawyer::CommandLine::SwitchGroup kreoSwitchGroup("Kreo Pregame Options");
  kreoSwitchGroup.insert(Switch("enable-alias-analysis")
                             .argument("enable", booleanParser(Kreo::settings.enable_static_alias_analysis), "true")
                             .doc("Whether to output static traces."));

  kreoSwitchGroup.insert(Switch("enable-calling-convention-analysis")
                             .argument("enable", booleanParser(Kreo::settings.enable_calling_convention_analysis), "true")
                             .doc("Whether to try and determine which procedures actually use the "
                                  "\"this\" argument register, to narrow down the method "
                                  "candidate list."));

  kreoSwitchGroup.insert(Switch("enable-symbol-procedure-detection")
                             .argument("enable", booleanParser(Kreo::settings.enableSymbolProcedureDetection), "false")
                             .doc("Whether to \"cheat\" and use debug information/symbols to help "
                                  "detect the procedure list. Desirable if using Kreo in the real "
                                  "world, undesirable when evaluating Kreo's performance on "
                                  "un-stripped binaries."));

  kreoSwitchGroup.insert(Switch("base-offset-path")
                             .argument("path", anyParser(Kreo::settings.baseOffsetPath))
                             .doc("Path to base offset file, where base offset will be written. Note: base offset "
                                  "will be written as a hex value with no leading '0x'."));

  kreoSwitchGroup.insert(Switch("method-candidates-path")
                             .argument("path", anyParser(Kreo::settings.methodCandidatesPath))
                             .doc("Path to method candidates file. The method candidates will be "
                                  "written as a list of hex values, one per line, with no leading '0x'."));

  kreoSwitchGroup.insert(Switch("static-traces-path")
                             .argument("path", anyParser(Kreo::settings.staticTracesPath))
                             .doc("Path to where static traces will be stored. Static traces will be stored as "
                                  "sequences of hex values. All static traces will be written to the same file."));

  kreoSwitchGroup.insert(Switch("debug-function")
                             .argument("address", positiveIntegerParser(Kreo::settings.debugFunctionAddr))
                             .doc("Address of function to debug. Not a required setting, but if set, all debug "
                                  "information will related to the function in question will be printed. The address "
                                  "is an integer, absolute address (no base offset applied)."));

  P2::Engine *engine(P2::Engine::instance());

  Sawyer::CommandLine::Parser cmdParser(engine->commandLineParser(Kreo::purpose, Kreo::description));
  cmdParser.with(kreoSwitchGroup);
  Sawyer::CommandLine::ParserResult parserResult(cmdParser.parse(argc, argv));
  // loads command-line options from the kreoSwitchGroup into the global `Kreo::settings` object. Also does whatever the
  // `engine` expects to parse its own command-line options.
  parserResult.apply();

  if (Kreo::settings.methodCandidatesPath.empty()) {
    mlog[Sawyer::Message::FATAL] << "No method candidate path specified; see --help" << std::endl;
    exit(EXIT_FAILURE);
  }

  if (Kreo::settings.staticTracesPath.empty()) {
    mlog[Sawyer::Message::FATAL] << "No static traces path specified; see --help" << std::endl;
    exit(EXIT_FAILURE);
  }

  // The last argument passed to the pregame will be unprocessed by the switch group. Should be the final argument
  // passed to pregame.
  std::vector<std::string> specimen = parserResult.unreachedArgs();

  if (specimen.empty()) {
    mlog[Sawyer::Message::FATAL] << "no binary specimen specified; see --help\n";
    exit(EXIT_FAILURE);
  }

  // ==========================================================================
  // PERFORM ANALYSIS

  engine->settings().partitioner.splittingThunks = true;
  engine->settings().partitioner.findingImportFunctions = false;  // ignore functions from other files
  engine->settings().partitioner.findingExportFunctions = Kreo::settings.enableSymbolProcedureDetection;
  engine->settings().partitioner.findingSymbolFunctions = Kreo::settings.enableSymbolProcedureDetection;
  P2::PartitionerPtr partitioner(engine->partition(specimen));  // Create and run partitioner

  engine->runPartitionerFinal(partitioner);

  Rose::BinaryAnalysis::Disassembler::BasePtr disassembler(engine->obtainDisassembler());

  // not sure what the "right" way to get the min address is, so for now we just
  // find the start of the first element in the map. A more robust possibility
  // is to find the segment containing the main function, or to look up a
  // segment by name.
  Rose::BinaryAnalysis::MemoryMapPtr memoryMap = engine->memoryMap();
  rose_addr_t baseOffset = memoryMap->nodes().begin()->key().least();
  mlog[DEBUG] << "Detected minimum address as " << baseOffset << std::endl;
  std::ofstream(Kreo::settings.baseOffsetPath) << std::hex << baseOffset << std::endl;

  std::ofstream methodCandidatesStream(Kreo::settings.methodCandidatesPath);

  std::ofstream staticTracesStream;
  if (Kreo::settings.enable_static_alias_analysis) {
    staticTracesStream = std::ofstream(Kreo::settings.staticTracesPath, std::ios_base::out | std::ios_base::binary);
  }

  int numMethodsFound(0);
  int i(0);

#pragma omp parallel for
  for (const P2::FunctionPtr &proc : partitioner->functions()) {
    const size_t relAddr(proc->address() - baseOffset);

#pragma omp critical(printStderr)
    {
      mlog[DEBUG] << "Analyze function " << std::hex << "0x" << relAddr << std::dec << ", which is " << ++i
                  << " out of " << partitioner->functions().size() << std::endl;
    }

    // there's already a conditional for thunks in the static analysis part, but if static analysis is disabled that
    // won't be reached. We're essentially checking two conditions: 1., before static analysis, that it's not a thunk,
    // and 2., after static analysis, that it uses the this pointer.
    if (proc->isThunk()) {
      continue;
    }

    bool usesThisPointer(true);  // assume it uses this pointer, possible set to false if static analysis is enabled and
                                 // finds that the register is not in fact used.

    if (Kreo::settings.enable_static_alias_analysis || Kreo::settings.enable_calling_convention_analysis) {
      auto analysisResult(Kreo::analyzeProcedure(partitioner, disassembler, proc, baseOffset));

      if (Kreo::settings.enable_static_alias_analysis) {
#pragma omp critical(printStaticTrace)
        {
          size_t meaningfulTraces{};
          if ((meaningfulTraces = analysisResult.numMeaningfulTraces()) > 0) {
            staticTracesStream << "# Analysis from procedure " << proc->name() << " @ " << std::hex << relAddr << " ("
                               << meaningfulTraces << " trace" << (meaningfulTraces == 1 ? "" : "s")
                               << "):" << std::endl
                               << analysisResult;
          }
        }
      }

      if (Kreo::settings.enable_calling_convention_analysis) {
        usesThisPointer = analysisResult.usesThisPointer;
      }
    }

    if (usesThisPointer) {
#pragma omp critical(incrementNumMethods)
      {
        numMethodsFound++;
        methodCandidatesStream << std::hex << relAddr << std::endl;
      }
    }
  }

  mlog[DEBUG] << "Final statistics:\n"
              << "    Detected " << numMethodsFound << " methods from " << partitioner->functions().size()
              << " total procedures." << std::endl;
}
