#include <rose.h>

#include <Rose/BinaryAnalysis/Partitioner2/Engine.h>
#include <Rose/BinaryAnalysis/Partitioner2/GraphViz.h>
#include <Rose/Diagnostics.h>
#include <Sawyer/CommandLine.h>

using namespace Rose::BinaryAnalysis;
using namespace Rose::Diagnostics;

static const char purpose[] =
    "Generates a list of function candidates for a given binary specimen.";
static const char description[] =
    "This tool disassembles the specified file and generates prints a list of "
    "function candidates to standard output. Functions are printed in decimal, "
    "a single function per line. Pass in --partition-split-thunks if you want "
    "the tool to list true functions and not thunk functions.";

// Unhard code this potentially (though this is a known constant)
static constexpr int kMinAddress{0x400000};

int main(int argc, char *argv[]) {
  ROSE_INITIALIZE;

  auto engine = Partitioner2::Engine::instance();

  std::vector<std::string> specimen =
      engine->parseCommandLine(argc, argv, purpose, description)
          .unreachedArgs();
  if (specimen.empty()) {
    mlog[FATAL] << "no binary specimen specified; see --help\n";
    exit(EXIT_FAILURE);
  }

  Partitioner2::Partitioner partitioner =
      engine->partition(specimen);  // Create and run partitioner
  engine->runPartitionerFinal(partitioner);

  for (const Partitioner2::Function::Ptr &function : partitioner.functions()) {
    // Print function candidates, excluding thunk functions
    if (!function->isThunk()) {
      std::cout << function->address() - kMinAddress << std::endl;
    }
  }

  delete engine;
}
