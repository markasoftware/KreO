#include "pdb_output_analyzer.h"

#include <fstream>
#include <sstream>
#include <string>

static constexpr std::string_view kTypesId{"*** TYPES"};

static constexpr std::string_view kClassId{"LF_CLASS"};
static constexpr std::string_view kFieldListId{"LF_FIELDLIST"};

static constexpr std::string_view kForwardRef{", FORWARD REF, "};
static constexpr std::string_view kStaticId{", STATIC, "};
static constexpr std::string_view kOneMethod{"= LF_ONEMETHOD, "};

static constexpr std::string_view kFieldListTypeId{"field list type "};

static constexpr std::string_view kClassNameId{"class name = "};
static constexpr std::string_view kFieldIndexId{"index = "};
static constexpr std::string_view kNameId{"name = "};
static constexpr std::string_view kUniqueName{"unique name = "};

static constexpr std::string_view kBlank{""};

/// @brief checks to see if string contains substr, returning true if str
/// contains substr, false otherwise.
static inline bool Contains(const std::string &str,
                            const std::string_view &substr) {
  return str.find(substr) != std::string::npos;
}

/// @brief Iterates the stream to the next type. Assumed that the stream is at
/// some previous type or between types. For example, given the stream pointer
/// is pointing to the start of the following example:
///
/// ```
///   # members = 1,  field list type 0x10e6,
///   Derivation list type 0x0000, VT shape type 0x0000
///   Size = 1, class name = __scrt_no_argv_policy, unique name =
///   ?AU__scrt_no_argv_policy@@, UDT(0x000010e7)
///
/// ```
///
/// Then the stream will be moved past the lines above to the last empty
/// line.
///
/// @param stream The stream to iterate through
/// @param last_line The last line that was read from the stream
/// @return True if a new type was found, false if not (the fstream reached the
/// end of the file or some other issue with the file stream).
static bool IterateToNewType(std::fstream &stream,
                             const std::string &last_line) {
  std::string line = last_line;

  do {
    if (line == kBlank) {
      return true;
    }
  } while (std::getline(stream, line));

  return false;
}

/// @brief Strict checking of std::getline. Calls std::getline and throws
/// runtime_error if std::getline failed.
static void MustGetLine(std::fstream &stream, std::string &out_str,
                        const std::string &error_str) {
  if (!std::getline(stream, out_str)) {
    throw std::runtime_error("MustGetLine failed, " + error_str);
  }
}

std::pair<std::shared_ptr<std::map<uint32_t, ClassInfo>>,
          std::shared_ptr<std::map<uint32_t, FieldList>>>
AnalyzePdbDump(const std::string &fname) {
  std::fstream fstream(fname);

  if (!fstream.is_open()) {
    throw std::runtime_error("failed to open file named " + fname);
  }

  std::string line;

  // Move stream to start of types (past file header)
  bool types_found{false};
  while (std::getline(fstream, line)) {
    if (Contains(line, kTypesId)) {
      types_found = true;
      break;
    }
  }
  if (!types_found) {
    throw std::runtime_error("failed to find start of types");
  }

  auto class_info = std::make_shared<std::map<uint32_t, ClassInfo>>();
  auto field_list_info = std::make_shared<std::map<uint32_t, FieldList>>();

  // Call IterateToNewType to iterate to the next type and std::getline to get
  // the first line in the type
  while (IterateToNewType(fstream, line) && std::getline(fstream, line)) {
    if (Contains(line, kClassId)) {
      ClassInfo ci;

      // Extract location of class
      uint32_t address{};
      {
        std::stringstream ss;
        ss << std::hex << line;

        if (!(ss >> address)) {
          throw std::runtime_error("failed to get class address");
        }
      }

      MustGetLine(fstream, line, "failed to get second class line");
      if (Contains(line, kForwardRef)) {
        // Forward refs are ignored
        continue;
      }

      // Extract field list type
      {
        auto loc = line.find(kFieldListTypeId);
        if (loc == std::string::npos) {
          throw std::runtime_error("couldn't find field list id");
        }

        std::stringstream ss;
        ss << std::hex << line.substr(loc + kFieldListTypeId.size());
        if (!(ss >> ci.field_list)) {
          throw std::runtime_error("field list type failed");
        }
      }

      MustGetLine(fstream, line, "failed to get third class line");
      MustGetLine(fstream, line, "failed to get fourth class line");

      // Extract class name
      {
        auto loc = line.find(kClassNameId);
        if (loc == std::string::npos) {
          throw std::runtime_error("couldn't find class name id");
        }

        std::stringstream ss(line.substr(loc + kClassNameId.size()));
        if (!(ss >> ci.class_name)) {
          throw std::runtime_error("getting class name failed");
        }
        ci.class_name = ci.class_name.substr(0, ci.class_name.size() - 1);
      }

      // Extract unique name
      {
        auto loc = line.find(kUniqueName);
        if (loc == std::string::npos) {
          throw std::runtime_error("couldn't find unique class name");
        }

        std::stringstream ss(line.substr(loc + kUniqueName.size()));
        if (!(ss >> ci.mangled_class_name)) {
          throw std::runtime_error("getting mangled class name failed");
        }
        ci.mangled_class_name =
            ci.mangled_class_name.substr(0, ci.mangled_class_name.size() - 1);
      }

      // Insert class info to class info map
      class_info->insert(std::pair(address, ci));
    } else if (Contains(line, kFieldListId)) {
      FieldList fl;

      // Extract address of field list
      uint32_t address{0};
      {
        std::stringstream ss;
        ss << std::hex << line;
        if (!(ss >> address)) {
          throw std::runtime_error("getting address failed");
        }
      }

      // Extract list contents
      while (true) {
        MustGetLine(fstream, line, "failed to get line from field list");

        if (Contains(line, kOneMethod)) {
          // Comment at end of line indicates additional content associated with
          // list element on next line
          while (line.find_last_of(',') == line.size() - 2) {
            std::string next_line;
            MustGetLine(fstream, next_line, "failed to get next method line");
            line += next_line;
          }

          // Check if it is a static method, continue if it is
          if (Contains(line, kStaticId)) {
            continue;
          }

          std::string method_name;
          uint32_t method_index;

          // it is a method, get name and index
          {
            std::stringstream ss;
            auto loc = line.find(kFieldIndexId);
            if (loc == std::string::npos) {
              throw std::runtime_error(std::string("failed to find ") +
                                       kFieldIndexId.data());
            }
            ss << std::hex << line.substr(loc + kFieldIndexId.size());
            if (!(ss >> method_index)) {
              throw std::runtime_error("failed to get method index");
            }
          }

          {
            auto loc = line.find(kNameId);
            if (loc == std::string::npos) {
              throw std::runtime_error(
                  std::string("could not find name id in str ") + line);
            }
            std::stringstream ss;
            ss << line.substr(loc + kNameId.size());
            if (!(ss >> method_name)) {
              throw std::runtime_error("failed to get method name");
            }
            method_name = method_name.substr(1, method_name.size() - 2);
          }

          fl.method_index_list.push_back(std::pair(method_name, method_index));
        } else if (line == kBlank) {
          break;
        }
      }

      if (fl.method_index_list.size() > 0) {
        field_list_info->insert(std::pair(address, fl));
      }
    } else {
      // TODO probably nothing
    }
  }

  return std::pair(class_info, field_list_info);
}
