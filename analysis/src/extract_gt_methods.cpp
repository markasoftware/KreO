#include "json_loader.h"
#include <set>
#include <sstream>

static constexpr int kBaseAddr{0x400000};

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "usage: ./extract_gt_methods <path/to/json>" << std::endl;
    return EXIT_FAILURE;
  }

  boost::json::object structures =
      JsonLoader::LoadData(argv[1]).as_object()["structures"].as_object();

    // Put in set to sort
    std::set<int> method_addrs;
  for (auto &cls : structures) {
    for (auto &method : cls.value().as_object()["methods"].as_object()) {
        std::string ea_str = method.value().as_object()["ea"].as_string().c_str();
        std::stringstream ss;
        ss << std::hex << ea_str;
        int out;
        ss >> out;
        out -= kBaseAddr;
        method_addrs.insert(out);
    }
  }

    // print sorted set 
  for (int ii : method_addrs) {
    std::cout << ii << std::endl;
  }

  return 0;
}
