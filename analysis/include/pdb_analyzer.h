#pragma once

#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <vector>
#include <set>

using type_id_t = uint32_t;
using virtual_address_t = uint64_t;

struct ClassInfo {
  std::string mangled_class_name;
  std::string class_name;
  virtual_address_t virtual_address{};
  type_id_t field_list{};
  std::set<type_id_t> parent_classes{};

  friend std::ostream &operator<<(std::ostream &os, const ClassInfo &ci) {
    os << "{'" << ci.mangled_class_name << "' class name: " << ci.class_name
       << ", virtual address: " << ci.virtual_address << ", field list: 0x"
       << std::hex << ci.field_list << std::dec << "}";
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

  friend std::ostream &operator<<(std::ostream &os, const MethodInfo &mi) {
    os << "{'" << mi.name << "' type id: " << std::hex << mi.type_id << std::dec
       << ", virtual address: 0x" << mi.virtual_address << "}";
    return os;
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
      os << it;
    }
    os << "}";
    return os;
  }
};

struct HeaderData {
  std::string name;
  virtual_address_t virtual_size{};
  virtual_address_t virtual_address{};

  friend std::ostream &operator<<(std::ostream &os, const HeaderData &hd) {
    os << "{name: " << hd.name << ", virtual size: 0x" << std::hex
       << hd.virtual_size << ", virtual address: 0x" << hd.virtual_address
       << std::dec << "}";
    return os;
  }
};

class PdbAnalyzer {
 public:
  void AnalyzePdbDump(const std::string &fname);

  std::shared_ptr<std::map<type_id_t, ClassInfo>> get_class_info() const {
    return ci_;
  }

  std::shared_ptr<std::map<std::string, MethodList>> get_method_list() const {
    return ml_;
  }

 private:
  static constexpr virtual_address_t kDefaultBaseAddress{0x00400000};

  static constexpr std::string_view kTypesSection{"*** TYPES"};
  static constexpr std::string_view kSectionHeadersSection{
      "*** SECTION HEADERS"};
  static constexpr std::string_view kOriginalSectionHeaders{
      "*** ORIGINAL SECTION HEADERS"};
  static constexpr std::string_view kPublicsSection{"*** PUBLICS"};
  static constexpr std::string_view kSymbolsSection{"*** SYMBOLS"};
  static constexpr std::string_view kClassId{"LF_CLASS"};
  static constexpr std::string_view kStructureId{"LF_STRUCTURE"};
  static constexpr std::string_view kGlobals{"*** GLOBALS"};
  static constexpr std::string_view kFieldListId{"LF_FIELDLIST"};
  static constexpr std::string_view kBaseClassId{" LF_BCLASS"};

  static constexpr std::string_view kModuleSection{"** Module: "};

  static constexpr std::string_view kSectionHeaderNum{"SECTION HEADER #"};

  static constexpr std::string_view kForwardRef{", FORWARD REF, "};
  static constexpr std::string_view kStaticId{", STATIC, "};
  static constexpr std::string_view kOneMethod{"= LF_ONEMETHOD, "};

  static constexpr std::string_view kFieldListTypeId{"field list type "};

  static constexpr std::string_view kClassNameId{"class name = "};
  static constexpr std::string_view kFieldIndexId{"index = "};
  static constexpr std::string_view kNameId{"name = "};
  static constexpr std::string_view kUniqueName{"unique name = "};
  static constexpr std::string_view kTypeId{"type = "};

  static constexpr std::string_view kBlank{""};

  static constexpr std::string_view kThisCall{"__thiscall"};

  static constexpr std::string_view kGproc32{"S_GPROC32"};

  static constexpr std::string_view kNoType{"T_NOTYPE"};

  /// @brief Identify all types in the pdb file
  /// @param stream
  void FindTypes(std::fstream &fstream);

  /// @brief Identify section headers in the pdb file
  void FindSectionHeaders(std::fstream &fstream);

  /// @brief Identify all publics in the pdb file
  void FindSymbols(std::fstream &fstream);

  /// @brief Identify all inheritance relationships in the pdb file. Must be called after FindTypes.
  void FindInheritanceRelationships(std::fstream &fstream);

  static void SeekToSectionHeader(std::fstream &fstream,
                                  const std::string_view &header);

  static void GetHexValueAfterString(const std::string &line,
                                     const std::string_view &str,
                                     uint32_t &out_value);

  static void GetStrValueAfterString(const std::string &line,
                                     const std::string_view &str,
                                     std::string &out_str);

  static void GetQuotedStrAfterString(const std::string &line,
                                      const std::string_view &str,
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
  std::shared_ptr<std::map<std::string, MethodList>> ml_;

  std::map<type_id_t, type_id_t> fieldlist_to_class_id_map_;
  std::map<type_id_t, std::string> forward_ref_type_to_unique_name_;
  std::map<std::string, type_id_t> unique_name_to_type_id_;

  std::map<int, HeaderData> h_data_;

  std::map<type_id_t, virtual_address_t> type_id_to_virtual_address_map_;
};
