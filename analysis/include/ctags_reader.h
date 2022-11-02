#pragma once

#include <set>
#include <string>
#include <string_view>

struct CtagsData {
  std::string name;
  // TODO consider inheritance relationships
  //   std::set<std::string> parent_objects;

  friend bool operator<(const CtagsData &o1, const CtagsData &o2) {
    return o1.name.compare(o2.name);
  }
};

class CtagsReader {
 public:
  void Read(const std::string &fname);

  inline const std::set<CtagsData> &get_parsed_ctag_data() const {
    return parsed_ctag_data_;
  }

  inline std::set<std::string> GenerateCtagsObjectList() const {
    std::set<std::string> obj_list;
    for (const auto &it : parsed_ctag_data_) {
      obj_list.insert(it.name);
    }
    return obj_list;
  }

 private:
  static constexpr std::string_view kKind{"kind"};
  static constexpr std::string_view kName{"name"};

  static constexpr std::string_view kStruct{"struct"};
  static constexpr std::string_view kClass{"class"};
  static constexpr std::string_view kScope{"scope"};
  static constexpr std::string_view kScopeKind{"scopeKind"};
  static constexpr std::string_view kNamespace{"namespace"};

  std::set<CtagsData> parsed_ctag_data_;
};
