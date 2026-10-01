#include "osdd/cas.hpp"
#include "osdd/http_server.hpp"
#include "osdd/index.hpp"
#include "osdd/manifest.hpp"

#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
void usage() {
  std::cerr << "Usage:\n"
            << "  osdd-repository put <root> <file>\n"
            << "  osdd-repository commit <root> <repository> <generation> <mappings.tsv>\n"
            << "  osdd-repository rebuild <root>\n"
            << "  osdd-repository resolve <root> <repository> <logical-path>\n"
            << "  osdd-repository serve <root> [port]\n"
            << "TSV columns: path, digest, component, media-type, integrity (last three optional).\n";
}

osdd::Mapping parse_mapping(const std::string& line) {
  std::vector<std::string> values;
  std::istringstream input(line);
  std::string value;
  while (std::getline(input, value, '\t')) values.push_back(value);
  if (values.size() < 2 || values.size() > 5) throw std::invalid_argument("invalid mapping TSV row");
  values.resize(5);
  return {values[0], values[1], values[2], values[3], values[4]};
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc < 3) { usage(); return 2; }
    const std::string command = argv[1];
    const std::filesystem::path root = argv[2];
    osdd::Cas cas(root / "cas");
    osdd::ManifestStore manifests(root / "manifests", cas);
    if (command == "put" && argc == 4) {
      std::ifstream input(argv[3], std::ios::binary);
      if (!input) throw std::runtime_error("cannot open input file");
      std::cout << cas.put(input) << '\n';
    } else if (command == "commit" && argc == 6) {
      std::ifstream input(argv[5]);
      if (!input) throw std::runtime_error("cannot open mapping file");
      osdd::Generation generation{argv[3], argv[4], {}};
      std::string line;
      while (std::getline(input, line)) if (!line.empty() && line.front() != '#') generation.mappings.push_back(parse_mapping(line));
      manifests.commit(std::move(generation));
      osdd::Index index(root / "index.sqlite");
      index.rebuild(manifests);
    } else if (command == "rebuild" && argc == 3) {
      osdd::Index index(root / "index.sqlite");
      index.rebuild(manifests);
    } else if (command == "resolve" && argc == 5) {
      osdd::Index index(root / "index.sqlite");
      const auto mapping = index.resolve(argv[3], argv[4]);
      if (!mapping) return 1;
      std::cout << mapping->digest << '\n';
    } else if (command == "serve" && (argc == 3 || argc == 4)) {
      osdd::Index index(root / "index.sqlite");
      index.rebuild(manifests);
      const int port = argc == 4 ? std::stoi(argv[3]) : 8080;
      if (port < 1 || port > 65535) throw std::invalid_argument("invalid port");
      osdd::HttpServer(cas, index).run(static_cast<std::uint16_t>(port));
    } else { usage(); return 2; }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << '\n';
    return 1;
  }
}
