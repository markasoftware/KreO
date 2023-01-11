// #pragma once

// #include <boost/json.hpp>
// #include <map>
// #include <memory>
// #include <optional>
// #include <set>

// class PdbResults {
//  public:
//   PdbResults(const std::vector<ClassData> &class_data);

//   /// @brief Combines elements in the class info and field list maps that have
//   /// the same class name (dbc file sometimes produces identical class
//   /// structures that share field list elements for some reason). This should
//   /// really be investigated. For example, std::exception is listed twice as a
//   /// type.
//   void CombineClasses();

//   boost::json::value ToJson() const;

//  private:
//   static constexpr std::string_view kVersion{"1.0.0"};

//   std::vector<ClassData> class_data_;
// };
