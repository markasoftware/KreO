#include <iostream>

#include "json_loader.h"
#include "pdb_organizer.h"
#include "pdb_parser.h"
#include "pdb_results_generator.h"

// ============================================================================
int main(int argv, char *argc[]) {
  if (argv != 2) {
    std::cerr << "Usage: ./analyze_pdb_dump <path-to-arguments-file>\n\tWhere "
                 "<path-to-arguments-file> is a json file containing the "
                 "arguments for running analysis."
              << std::endl;
    return EXIT_FAILURE;
  }

  std::string arguments_json_file = argc[1];

  boost::json::value arguments = JsonLoader::LoadData(arguments_json_file);

  auto file_it = arguments.as_object().find("pdb_file");

  if (file_it == arguments.as_object().end()) {
    std::cerr << "pdb_file must be in the arguments json file" << std::endl;
    return EXIT_FAILURE;
  }

  auto basedir_it = arguments.as_object().find("base_directory");
  if (basedir_it == arguments.as_object().end()) {
    std::cerr << "base_directory must be in the arguments json file"
              << std::endl;
    return EXIT_FAILURE;
  }

  size_t pos = arguments_json_file.find_last_of("\\/");
  std::string arguments_json_dir = "";
  if (pos != std::string::npos) {
    arguments_json_dir = arguments_json_file.substr(0, pos) + "/";
  }

  std::string base_dir =
      arguments_json_dir + basedir_it->value().as_string().c_str();

  std::string pdb_file = base_dir + "/project.dump";
  std::string out_file = base_dir + "/gt-results.json";

  PdbParser parser(pdb_file);
  parser.ParseTypeData();
  parser.ParseSectionHeaders();
  parser.ParseSymbols();

  PdbOrganizer organizer;
  organizer.Organize(parser);

  PdbResultsGenerator generator(organizer);

  std::ofstream out(out_file);
  out << boost::json::serialize(generator.ToJson());

  return EXIT_SUCCESS;
}
