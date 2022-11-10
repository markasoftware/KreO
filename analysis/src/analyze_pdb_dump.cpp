#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ctags_reader.h"
#include "pdb_analyzer.h"
#include "pdb_results.h"

// ============================================================================
int main(int argv, char *argc[]) {
  if (argv != 2 && argv != 3) {
    std::cerr
        << "Usage: ./analyze_pdb_dump <path-to-pdb-dump-file> "
           "<path-to-ctags-file>\n\tWhere "
           "<path-to-pdb-dump-file> is a file generated by running cvdump.exe, "
           "piping stdout to a dump file. And <path-to-ctags-file> is a json "
           "file containing the ctags output from running ctags on the source "
           "code"
        << std::endl;
    return EXIT_FAILURE;
  }

  std::string pdb_file(argc[1]);

  std::optional<std::string> ctags_json{std::nullopt};
  if (argv == 3) {
    ctags_json = std::string(argc[2]);
  }

  // Analyze PDB dump
  PdbAnalyzer analyzer;
  analyzer.AnalyzePdbDump(pdb_file);

  PdbResults results(analyzer.get_class_info(), analyzer.get_method_list());
  // results.CombineClasses();

  // Read in ctags output
  if (ctags_json.has_value()) {
    CtagsReader reader;
    reader.Read(*ctags_json);

    // Remove all classes that aren't in the ctags output
    results.RemoveAllBut(reader.GenerateCtagsObjectList());
  }

  std::cout << boost::json::serialize(results.ToJson()) << std::endl;

  return EXIT_SUCCESS;
}