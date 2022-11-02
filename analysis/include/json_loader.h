#include <boost/json.hpp>
#include <fstream>
#include <iostream>
#include <string>

class JsonLoader {
 public:
  static boost::json::value LoadData(const std::string& fname) {
    std::ifstream file;
    file.open(fname, std::ios_base::in | std::ios_base::ate);

    if (file.fail()) {
      std::cerr << "failed to open file" << std::endl;
      return nullptr;
    }

    // Read file into buffer
    auto f_size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<char> buf(f_size);
    file.read(buf.data(), buf.size());

    if (file.fail()) {
      std::cerr << "failed to read from file" << std::endl;
      return nullptr;
    }

    file.close();

    boost::json::error_code ec;
    boost::json::stream_parser parser;
    parser.write(buf.data(), buf.size(), ec);

    if (ec) {
      return nullptr;
    }

    parser.finish(ec);

    if (ec) {
      return nullptr;
    }

    return parser.release();
  }
};
