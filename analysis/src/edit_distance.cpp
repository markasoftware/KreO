#include <boost/json.hpp>
#include <iostream>

#include "json_loader.h"

int main(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: ./edit_distance <path-to-ground-truth-json> "
                 "<path-to-generated-json>"
              << std::endl;
    return EXIT_FAILURE;
  }

  auto ground_truth_json = JsonLoader::LoadData(std::string(argv[1]));
  auto generated_json = JsonLoader::LoadData(std::string(argv[2]));
}
