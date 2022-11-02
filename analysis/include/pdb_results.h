#pragma once

#include <map>
#include <set>
#include <memory>
#include <optional>

#include <boost/json.hpp>
#include "pdb_analyzer.h"

class PdbResults {
 public:
  PdbResults(std::shared_ptr<std::map<uint32_t, ClassInfo>> ci,
             std::shared_ptr<std::map<uint32_t, MethodList>> fl);

  std::optional<uint32_t> FindClassIndex(const std::string &classname);
  /// @brief Combines elements in the class info and field list maps that have
  /// the same class name (dbc file sometimes produces identical class
  /// structures that share field list elements for some reason). This should
  /// really be investigated. For example, std::exception is listed twice as a
  /// type.
  void CombineClasses();

  friend std::ostream &operator<<(std::ostream &os, const PdbResults &results);

  void RemoveAllBut(const std::set<std::string> &classes);

  boost::json::value ToJson() const;

 private:
  std::shared_ptr<std::map<uint32_t, ClassInfo>> ci_;
  std::shared_ptr<std::map<uint32_t, MethodList>> ml_;
};
