#pragma once

#include <boost/json.hpp>

class PdbOrganizer;

class PdbResultsGenerator {
 public:
  PdbResultsGenerator(const PdbOrganizer &organizer);

  boost::json::value ToJson() const;

 private:
  static constexpr std::string_view kVersion{"1.0.0"};

  boost::json::object json_generate_base_class_info(size_t base_class) const;

  boost::json::object json_generate_method_info(const std::string &name,
                                                const std::string &addr) const;

  const PdbOrganizer &organizer_;
};
