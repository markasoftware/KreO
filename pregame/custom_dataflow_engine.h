/**
 * This file is mostly copied from Rose. Our main concern is being able to control the order of
 * iteration, because since we are a DAG we know the order in which to perform dataflow; we don't
 * end up with racers.
 *
 * Specifically, the main difference is that the modified constructor allows an initial worklist order.
 */

#pragma once

#include <Rose/BinaryAnalysis/DataFlow.h>
#include <Rose/BinaryAnalysis/InstructionSemantics/DataFlowSemantics.h>
#include <Rose/Diagnostics.h>
#include <Rose/Exception.h>
#include <Sawyer/DistinctList.h>
#include <Sawyer/GraphTraversal.h>

#include <boost/lexical_cast.hpp>
#include <list>
#include <sstream>
#include <string>
#include <vector>

namespace KreoRoseMods {
class DataFlow {
 public:
  /** Data-flow exception base class. */
  class Exception : public Rose::Exception {
   public:
    explicit Exception(const std::string &s) : Rose::Exception(s) {}
  };

  /** Exceptions when a fixed point is not reached. */
  class NotConverging : public Exception {
   public:
    explicit NotConverging(const std::string &s) : Exception(s) {}
  };

  static Sawyer::Message::Facility mlog;  ///< diagnostics for data-flow

  /** Trivial path feasibility predicate.
   *
   * This path feasibility predicate always returns true. This causes the data-flow to be path insensitive and always
   * follow all edges when propagating states along edges. */
  template <class CFG, class State>
  class PathAlwaysFeasible {
   public:
    bool operator()(const CFG &, const typename CFG::Edge &, const State &, const State &) { return true; }
  };

  /** Data-flow engine.
   *
   *  The data-flow engine traverses the supplied control flow graph, runs the transfer function at each vertex, and
   * merges data state objects as necessary.
   *
   *  The template arguments are:
   *
   *  @li @p CFG is the type for the control flow graph.  It must implement the Sawyer::Container::Graph API and have
   * vertex ID numbers of type @c size_t.  The data-flow will follow the edges of this graph, invoking a transfer
   * function (see below) at each vertex.  The vertices are usually basic blocks or instructions, although they can be
   * anything that the transfer function can understand.  For instance, its possible for a called function to be
   * represented by a special vertex that encapsulates the entire effect of the function, either encoded in the vertex
   * itself or as a special case in the transfer function.  Although a normal CFG can be used, one often passes either a
   * subgraph of the CFG or an entirely different kind of control flow graph. Subgraphs are useful for solving data-flow
   * for a single function, and different types are useful for passing additional/different information to the transfer
   *      function.
   *
   *  @li @p State is an object that stores (or points to) an analysis state: the values that the data flow is
   * manipulating. A state object is attached to each vertex of the CFG to represent the data-flow state at that vertex.
   * For instance, the state might be a map of abstract locations (e.g., variables) and their current values, such as an
   *      instruction semantics @c State object.  If the @p State type is a pointer to a state, then since the engine
   * doesn't implement any particular ownership paradigm, the @p State type should be some kind of shared-ownership
   * pointer so that the objects are properly freed.  See the description of the MergeFunction below for more info about
   * the values stored in state object.
   *
   *  @li @p TransferFunction is a functor that is invoked at each CFG vertex to create a new vertex output state from
   * its input state. The functor is called with three arguments: a const reference to the control flow graph, the CFG
   *      vertex ID for the vertex being processed, and the incoming state for that vertex.  The call should return a
   * new outgoing state. For instance, if the CFG vertices contain SgAsmInstruction nodes and the @p State is a pointer
   * to an instruction semantics state, then the transfer function would most likely perform these operations: create a
   * new state by cloning the incoming state, attach the new state to an instruction semantics dispatcher (virtual CPU),
   *      call the dispatcher's @c processInstruction, return the new state.  The transfer functor should also have a @p
   *      toString method that returns an optional string containing one or more lines, which is used for debugging
   * (this method often just delegates to a similar method in the state, but that's not always possible, which is why we
   *      define it here).
   *
   *  @li @p MergeFunction is a functor that takes two @p State objects and merges the second state into the first
   *      state. Therefore, the first argument should be a reference. The MergeFunction returns true if the first state
   *      changed, false if there was no change. In order for a data-flow to reach a fixed point the values must form a
   *      lattice and a merge operation should return a value which is the greatest lower bound. This implies that the
   *      lattice has a bottom element that is a descendent of all other vertices.  However, the data-flow engine is
   * designed to also operate in cases where a fixed point cannot be reached.
   *
   *  @li @p PathFeasibility is a predicate that returns true if the data-flow should traverse the specified CFG edge.
   * It's called with the following arguments: (1) the CFG, which it should accept as a const reference argument for
   *      efficiency's sake; (2) the edge that should be tested by this predicate; (3) the incoming state for the edge,
   * i.e., the outgoing state for the edge's source vertex; and (4) the outgoing state for the edge, i.e., the incoming
   * state for the edge's target vertex.
   *
   *  A common configuration for an engine is to use a control-flow graph whose vertices are basic blocks, whose @p
   * State is a pointer to an @ref InstructionSemantics::BaseSemantics::State "instruction semantics state", whose @p
   *  TransferFunction calls @ref InstructionSemantics::BaseSemantics::Dispatcher::processInstruction
   *  "Dispatcher::processInstruction", and whose @p MergeFunction calls the state's @ref
   *  InstructionSemantics::BaseSemantics::State::merge "merge" method.
   *
   *  The control flow graph and transfer function are specified in the engine's constructor.  The starting CFG vertex
   * and its initial state are supplied when the engine starts to run. */
  template <class CFG, class State, class TransferFunction, class MergeFunction,
            class PathFeasibility = PathAlwaysFeasible<CFG, State> >
  class Engine {
   public:
    using VertexStates = std::vector<State>; /**< Data-flow states indexed by vertex ID. */

   private:
    std::string name_;  // optional name for debugging
    const CFG &cfg_;
    TransferFunction &xfer_;
    MergeFunction merge_;
    VertexStates incomingState_;  // incoming data-flow state per CFG vertex ID
    VertexStates outgoingState_;  // outgoing data-flow state per CFG vertex ID
    using WorkList = Sawyer::Container::DistinctList<size_t>;
    WorkList workList_;           // CFG vertex IDs to be visited, last in first out w/out duplicates
    size_t maxIterations_;        // max number of iterations to allow
    size_t nIterations_;          // number of iterations since last reset
    PathFeasibility isFeasible_;  // predicate to test path feasibility

   public:
    /** Constructor.
     *
     *  Constructs a new data-flow engine that will operate over the specified control flow graph using the specified
     *  transfer function.  The control flow graph is incorporated into the engine by reference; the transfer functor is
     *  copied. */
    Engine(const CFG &cfg, TransferFunction &xfer, MergeFunction merge = MergeFunction(),
           PathFeasibility isFeasible = PathFeasibility())
        : cfg_(cfg),
          xfer_(xfer),
          merge_(merge),
          maxIterations_(-1),
          nIterations_(0),
          isFeasible_(isFeasible) {
      reset();
    }

    /** Data-flow control flow graph.
     *
     * Returns a reference to the control flow graph that's being used for the data-flow analysis. The return value is
     * the same control flow graph as which was supplied to the constructor. */
    const CFG &cfg() const { return cfg_; }

    /** Reset engine to initial state. */
    void reset(State initialState = State()) {
      ASSERT_this();
      incomingState_.clear();
      incomingState_.resize(cfg_.nVertices(), initialState);
      outgoingState_.clear();
      outgoingState_.resize(cfg_.nVertices(), initialState);
      workList_.clear();
      nIterations_ = 0;
    }

    /** Property: Name for debugging.
     *
     * This optional name will show up in debugging output.
     *
     * @{ */
    const std::string &name() const { return name_; }
    void name(const std::string &s) { name_ = s; }
    /** @} */

    /** Line prefix for debugging. */
    std::string prefix() const {
      if (name_.empty()) {
        return "";
      } else {
        return name_ + ": ";
      }
    }

    /** Max number of iterations to allow.
     *
     * Allow N number of calls to runOneIteration.  When the limit is exceeded a @ref NotConverging exception is
     * thrown.
     *
     * @{ */
    size_t maxIterations() const { return maxIterations_; }
    void maxIterations(size_t n) { maxIterations_ = n; }
    /** @} */

    /** Number of iterations run.
     *
     * The number of times runOneIteration was called since the last reset. */
    size_t nIterations() const { return nIterations_; }

    /** Runs one iteration.
     *
     * Runs one step of data-flow analysis by consuming the first item on the work list.  Returns false if the
     * work list is empty (before of after the iteration). */
    bool runOneIteration() {
      using namespace Rose::Diagnostics;
      if (!workList_.isEmpty()) {
        if (++nIterations_ > maxIterations_) {
          throw NotConverging(
              "data-flow max iterations reached"
              " (max=" +
              Rose::StringUtility::numberToString(maxIterations_) + ")");
        }
        size_t cfgVertexId = workList_.popFront();
        if (mlog[DEBUG]) {
          mlog[DEBUG] << prefix() << "runOneIteration: vertex #" << cfgVertexId << "\n";
          mlog[DEBUG] << prefix() << "  remaining worklist is {";
          for (size_t id : workList_.items()) mlog[DEBUG] << " " << id;
          mlog[DEBUG] << " }\n";
        }

        ASSERT_require2(cfgVertexId < cfg_.nVertices(),
                        "vertex " + boost::lexical_cast<std::string>(cfgVertexId) + " must be valid within CFG");
        typename CFG::ConstVertexIterator vertex = cfg_.findVertex(cfgVertexId);
        State state = incomingState_[cfgVertexId];
        if (mlog[DEBUG]) {
          mlog[DEBUG] << prefix() << "  incoming state for vertex #" << cfgVertexId << ":\n"
                      << Rose::StringUtility::prefixLines(xfer_.toString(state), prefix() + "    ") << "\n";
        }

        state = outgoingState_[cfgVertexId] = xfer_(cfg_, cfgVertexId, state);
        if (mlog[DEBUG]) {
          mlog[DEBUG] << prefix() << "  outgoing state for vertex #" << cfgVertexId << ":\n"
                      << Rose::StringUtility::prefixLines(xfer_.toString(state), prefix() + "    ") << "\n";
        }

        // Outgoing state must be merged into the incoming states for the CFG successors.  Any such incoming state that
        // is modified as a result will have its CFG vertex added to the work list.
        SAWYER_MESG(mlog[DEBUG]) << prefix() << "  forwarding vertex #" << cfgVertexId << " output state to "
                                 << Rose::StringUtility::plural(vertex->nOutEdges(), "vertices", "vertex") << "\n";
        for (const typename CFG::Edge &edge : vertex->outEdges()) {
          size_t nextVertexId = edge.target()->id();
          if (!isFeasible_(cfg_, edge, state, incomingState_[nextVertexId])) {
            SAWYER_MESG(mlog[DEBUG]) << prefix() << "    path to vertex #" << nextVertexId
                                     << " is not feasible, thus skipped\n";
          } else if (merge_(incomingState_[nextVertexId], state)) {
            if (mlog[DEBUG]) {
              mlog[DEBUG] << prefix() << "    merged with vertex #" << nextVertexId << " (which changed as a result)\n";
              mlog[DEBUG] << prefix() << "    merge state is: "
                          << Rose::StringUtility::prefixLines(
                                 xfer_.toString(incomingState_[nextVertexId]), prefix() + "      ", false)
                          << "\n";
            }
            workList_.pushBack(nextVertexId);
          } else {
            SAWYER_MESG(mlog[DEBUG]) << prefix() << "    merged with vertex #" << nextVertexId << " (no change)\n";
          }
        }
      }
      return !workList_.isEmpty();
    }

    /** Add a starting vertex. */
    void insertStartingVertex(size_t startVertexId, const State &initialState) {
      incomingState_[startVertexId] = initialState;
      workList_.pushBack(startVertexId);
    }

    /** Run data-flow until it reaches a fixed point.
     *
     * Run data-flow starting at the specified control flow vertex with the specified initial state until the state
     * converges to a fixed point or the maximum number of iterations is reached (in which case a @ref NotConverging
     * exception is thrown). */
    void runToFixedPoint() {
      while (runOneIteration())
        ;
    }

    /** Add starting point and run to fixed point.
     *
     * This is a combination of @ref reset, @ref insertStartingVertex, and @ref runToFixedPoint. */
    void runToFixedPoint(size_t startVertexId, const State &initialState) {
      reset();
      insertStartingVertex(startVertexId, initialState);
      while (runOneIteration())
        ;
    }

    /** KREO CUSTOM ADDITION
     * The worklist is processed from front to back.
     */
    void worklist(const WorkList &worklist) { workList_ = worklist; }

    /** Return the incoming state for the specified CFG vertex.
     *
     * This is a pointer to the incoming state for the vertex as of the latest data-flow iteration.  If the data-flow
     * has not reached this vertex then it is likely to be a null pointer. */
    State getInitialState(size_t cfgVertexId) const { return incomingState_[cfgVertexId]; }

    /** Set the initial state for the specified CFG vertex. */
    void setInitialState(size_t cfgVertexId, State state) { incomingState_[cfgVertexId] = state; }

    /** Return the outgoing state for the specified CFG vertex.
     *
     * This is a pointer to the outgoing state for the vertex as of the latest data-flow iteration. If the data-flow
     * has not processed this vertex then it is likely to be a null pointer. */
    State getFinalState(size_t cfgVertexId) const { return outgoingState_[cfgVertexId]; }

    /** All incoming states.
     *
     * Returns a vector indexed by vertex ID for the incoming state of each vertex as of the latest data-flow
     * iteration. States for vertices that have not yet been reached are null pointers. */
    const VertexStates &getInitialStates() const { return incomingState_; }

    /** All outgoing states.
     *
     * Returns a vector indexed by vertex ID for the outgoing state of each vertex as of the latest data-flow
     * iteration. States for vertices that have not yet been processed are null pointers. */
    const VertexStates &getFinalStates() const { return outgoingState_; }
  };
};

}  // namespace KreoRoseMods
