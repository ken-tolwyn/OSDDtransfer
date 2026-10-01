#pragma once

#include "osdd/cas.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace osdd {

struct Mapping {
  std::string path;
  std::string digest;
  std::string component;
  std::string media_type;
  std::string integrity;
};

struct Generation {
  std::string repository;
  std::string id;
  std::vector<Mapping> mappings;
};

class ManifestStore {
 public:
  ManifestStore(std::filesystem::path root, const Cas& cas);
  void commit(Generation generation);
  [[nodiscard]] Generation load(const std::string& repository,
                                const std::string& generation) const;
  [[nodiscard]] std::optional<std::string> current(const std::string& repository) const;
  [[nodiscard]] std::vector<std::string> repositories() const;
  static bool valid_name(const std::string& name);
  static bool valid_logical_path(const std::string& path);

 private:
  std::filesystem::path root_;
  const Cas& cas_;
};

}  // namespace osdd
