#pragma once

#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>

using type_id_t = uint32_t;
using virtual_address_t = uint64_t;

struct ClassInfo {
  std::string mangled_class_name;
  std::string class_name;
  virtual_address_t virtual_address{};
  type_id_t field_list{};

  friend std::ostream &operator<<(std::ostream &os, const ClassInfo &ci) {
    os << "{'" << ci.mangled_class_name << "' class name: " << ci.class_name
       << ", virtual address: " << ci.virtual_address
       << ", field_list: " << ci.field_list << "}";
    return os;
  }
};

struct MethodInfo {
  std::string name;
  type_id_t type_id{};
  virtual_address_t virtual_address{};

  friend bool operator==(const MethodInfo &mi1, const MethodInfo &mi2) {
    return mi1.name == mi2.name && mi1.type_id == mi2.type_id &&
           mi1.virtual_address == mi2.virtual_address;
  }
};

struct MethodList {
  std::vector<MethodInfo> method_index_list;

  friend std::ostream &operator<<(std::ostream &os, const MethodList fl) {
    os << "{";
    bool first = true;
    for (const auto it : fl.method_index_list) {
      if (!first) {
        os << ", ";
      } else {
        first = false;
      }
      // os << "{" << it.first << ", " << it.second << "}";
    }
    os << "}";
    return os;
  }
};

class PdbAnalyzer {
 public:
  void AnalyzePdbDump(const std::string &fname);

  std::shared_ptr<std::map<type_id_t, ClassInfo>> get_class_info() const {
    return ci_;
  }

  std::shared_ptr<std::map<type_id_t, MethodList>> get_method_list() const {
    return ml_;
  }

 private:
  /// @brief Identify all types in the pdb file
  /// @param stream
  void FindTypes(std::fstream &fstream);

  /// @brief Identify section headers in the pdb file
  void FindSectionHeaders(std::fstream &fstream);

  /// @brief Identify all publics in the pdb file
  void FindPublics(std::fstream &fstream);

  static void SeekToSectionHeader(std::fstream &fstream,
                                  const std::string_view &header);

  static void GetHexValueAfterString(const std::string &line,
                                     const std::string_view str,
                                     uint32_t &out_value);

  static void GetStrValueAfterString(const std::string &line,
                                     const std::string_view str,
                                     std::string &out_str);

  /// @brief checks to see if string contains substr, returning true if str
  /// contains substr, false otherwise.
  static bool Contains(const std::string &str, const std::string_view &substr);

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
  /// @return True if a new type was found, false if not (the fstream reached
  /// the end of the file or some other issue with the file stream).
  static bool IterateToNewType(std::fstream &stream,
                               const std::string &last_line);

  /// @brief Strict checking of std::getline. Calls std::getline and throws
  /// runtime_error if std::getline failed.
  static void MustGetLine(std::fstream &stream, std::string &out_str,
                          const std::string &error_str);

  std::shared_ptr<std::map<type_id_t, ClassInfo>> ci_;
  std::shared_ptr<std::map<type_id_t, MethodList>> ml_;
};
