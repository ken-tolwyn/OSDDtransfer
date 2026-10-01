#pragma once

#include <filesystem>
#include <istream>
#include <string>

namespace osdd {

class Cas {
 public:
  explicit Cas(std::filesystem::path root);
  [[nodiscard]] std::string put(std::istream& input);
  [[nodiscard]] bool contains(const std::string& digest) const;
  [[nodiscard]] std::filesystem::path object_path(const std::string& digest) const;
  static bool valid_digest(const std::string& digest);

 private:
  std::filesystem::path root_;
};

}  // namespace osdd
