#include "ctags_reader.h"

#include "json_loader.h"

// ============================================================================
void CtagsReader::Read(const std::string &fname) {
  auto json = JsonLoader::LoadData(fname);

  if (json == nullptr) {
    throw std::runtime_error("failed to read in ctags json data from file " +
                             fname);
  }

  for (const auto &it : json.as_array()) {
    const auto &it_obj = it.as_object();

    if (!it_obj.count(kKind.data())) {
      continue;
    }

    std::string kind = it_obj.find(kKind.data())->value().as_string().c_str();

    if (kind == kStruct || kind == kClass) {
      CtagsData data;
      data.name = it_obj.find(kName.data())->value().as_string().c_str();

      const auto &scope = it_obj.find(kScope.data());

      if (scope != it_obj.end()) {
        std::string scope_kind =
            it_obj.find(kScopeKind.data())->value().as_string().c_str();
        if (scope_kind == kNamespace) {
          data.name = scope->value().as_string().c_str() + std::string("::") +
                      data.name;
        }
      }

      parsed_ctag_data_.insert(data);
    }
  }
}
