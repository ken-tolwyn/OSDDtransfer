#pragma once

#include "osdd/manifest.hpp"

#include <sqlite3.h>

#include <filesystem>
#include <optional>
#include <string>

namespace osdd {

class Index {
 public:
  explicit Index(std::filesystem::path path);
  ~Index();
  Index(const Index&) = delete;
  Index& operator=(const Index&) = delete;

  void rebuild(const ManifestStore& manifests);
  [[nodiscard]] std::optional<Mapping> resolve(const std::string& repository,
                                               const std::string& path) const;
  [[nodiscard]] std::string sbom(const std::string& repository,
                                 const std::string& format) const;

 private:
  sqlite3* db_{};
};

}  // namespace osdd
