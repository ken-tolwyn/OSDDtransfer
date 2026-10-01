#include "osdd/cas.hpp"
#include "osdd/index.hpp"
#include "osdd/manifest.hpp"

#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace {
void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
}  // namespace

int main() {
  const auto root = std::filesystem::temp_directory_path() / ("osdd-test-" + std::to_string(std::random_device{}()));
  try {
    osdd::Cas cas(root / "cas");
    std::istringstream first("immutable bytes");
    const std::string digest = cas.put(first);
    expect(digest == "59d8792018a51a408d2738f31eedebd6fe9926cc4260fa168a38710bc51d7e30", "unexpected SHA-256");
    expect(cas.contains(digest), "object was not committed");
    std::istringstream duplicate("immutable bytes");
    expect(cas.put(duplicate) == digest, "CAS ingest is not idempotent");

    osdd::ManifestStore manifests(root / "manifests", cas);
    osdd::Generation generation{"release", "0001", {
      {"maven/central/org/example/demo/1.0/demo-1.0.jar", digest, "pkg:maven/org.example/demo@1.0", "application/java-archive", ""},
      {"maven/central/org/example/demo/1.0/demo-1.0.pom", digest, "pkg:maven/org.example/demo@1.0", "application/xml", ""},
    }};
    manifests.commit(generation);
    expect(manifests.current("release") == "0001", "CURRENT was not published");

    bool rejected = false;
    try {
      manifests.commit({"release", "0002", {{"npm/public/pkg/file.tgz", std::string(64, '0'), "pkg:npm/pkg@1", "", ""}}});
    } catch (const std::invalid_argument&) { rejected = true; }
    expect(rejected, "missing CAS reference was accepted");

    {
      osdd::Index index(root / "index.sqlite");
      index.rebuild(manifests);
      const auto mapping = index.resolve("release", "maven/central/org/example/demo/1.0/demo-1.0.jar");
      expect(mapping && mapping->digest == digest, "index did not resolve a mapping");
      const std::string sbom = index.sbom("release", "cyclonedx");
      expect(sbom.find("pkg:maven/org.example/demo@1.0") != std::string::npos, "SBOM omitted component");
      expect(sbom.find("pkg:maven/org.example/demo@1.0", sbom.find("pkg:maven/org.example/demo@1.0") + 1) != std::string::npos,
             "SBOM component fields missing");
    }

    std::filesystem::remove(root / "index.sqlite");
    osdd::Index rebuilt(root / "index.sqlite");
    rebuilt.rebuild(manifests);
    expect(rebuilt.resolve("release", generation.mappings[0].path).has_value(), "deleted index was not rebuildable");

    std::filesystem::remove_all(root);
    std::cout << "repository tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::filesystem::remove_all(root);
    std::cerr << error.what() << '\n';
    return 1;
  }
}
