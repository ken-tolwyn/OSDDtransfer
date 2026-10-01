#include "osdd/manifest.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <stdexcept>

namespace fs = std::filesystem;

namespace osdd {
namespace {
std::string escape(const std::string& value) {
  std::string result;
  for (const unsigned char ch : value) {
    if (ch == '"' || ch == '\\') result.push_back('\\');
    if (ch == '\n') result += "\\n";
    else if (ch == '\r') result += "\\r";
    else if (ch == '\t') result += "\\t";
    else if (ch < 0x20) throw std::invalid_argument("control character in manifest value");
    else result.push_back(static_cast<char>(ch));
  }
  return result;
}

std::string field(const std::string& line, const std::string& name) {
  const std::string marker = "\"" + name + "\":\"";
  auto position = line.find(marker);
  if (position == std::string::npos) throw std::runtime_error("invalid manifest: missing " + name);
  position += marker.size();
  std::string result;
  bool escaped = false;
  for (; position < line.size(); ++position) {
    const char ch = line[position];
    if (escaped) {
      if (ch == 'n') result.push_back('\n');
      else if (ch == 'r') result.push_back('\r');
      else if (ch == 't') result.push_back('\t');
      else if (ch == '"' || ch == '\\') result.push_back(ch);
      else throw std::runtime_error("invalid JSON escape");
      escaped = false;
    } else if (ch == '\\') escaped = true;
    else if (ch == '"') return result;
    else result.push_back(ch);
  }
  throw std::runtime_error("invalid manifest string");
}

std::string serialize(const Generation& generation) {
  std::string content = "{\"type\":\"osdd.manifest.v1\",\"repository\":\"" +
                        escape(generation.repository) + "\",\"generation\":\"" +
                        escape(generation.id) + "\"}\n";
  for (const auto& item : generation.mappings) {
    content += "{\"type\":\"mapping\",\"path\":\"" + escape(item.path) +
               "\",\"digest\":\"" + item.digest + "\",\"component\":\"" +
               escape(item.component) + "\",\"mediaType\":\"" + escape(item.media_type) +
               "\",\"integrity\":\"" + escape(item.integrity) + "\"}\n";
  }
  return content;
}
}  // namespace

ManifestStore::ManifestStore(fs::path root, const Cas& cas) : root_(std::move(root)), cas_(cas) {
  fs::create_directories(root_);
}

bool ManifestStore::valid_name(const std::string& name) {
  if (name.empty() || name.size() > 128) return false;
  return std::all_of(name.begin(), name.end(), [](const unsigned char ch) {
    return std::isalnum(ch) || ch == '.' || ch == '_' || ch == '-';
  });
}

bool ManifestStore::valid_logical_path(const std::string& path) {
  if (path.empty() || path.front() == '/' || path.back() == '/' || path.find('\\') != std::string::npos) return false;
  std::size_t start = 0;
  int segments = 0;
  while (start <= path.size()) {
    const auto end = path.find('/', start);
    const auto part = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (part.empty() || part == "." || part == "..") return false;
    ++segments;
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return segments >= 3;
}

void ManifestStore::commit(Generation generation) {
  if (!valid_name(generation.repository) || !valid_name(generation.id)) {
    throw std::invalid_argument("invalid repository or generation name");
  }
  std::sort(generation.mappings.begin(), generation.mappings.end(),
            [](const Mapping& left, const Mapping& right) { return left.path < right.path; });
  std::string previous;
  for (const auto& item : generation.mappings) {
    if (!valid_logical_path(item.path)) throw std::invalid_argument("invalid logical path: " + item.path);
    if (item.path == previous) throw std::invalid_argument("duplicate logical path: " + item.path);
    if (!Cas::valid_digest(item.digest) || !cas_.contains(item.digest)) {
      throw std::invalid_argument("manifest references missing CAS object: " + item.digest);
    }
    previous = item.path;
  }
  const std::string content = serialize(generation);
  const fs::path directory = root_ / generation.repository;
  const fs::path generations = directory / "generations";
  fs::create_directories(generations);
  const fs::path final_path = generations / (generation.id + ".jsonl");
  if (fs::exists(final_path)) {
    std::ifstream existing(final_path, std::ios::binary);
    const std::string old((std::istreambuf_iterator<char>(existing)), {});
    if (old != content) throw std::runtime_error("immutable generation already exists with different content");
  } else {
    const fs::path temporary = generations / ("." + generation.id + ".tmp");
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    output << content;
    output.close();
    if (!output) throw std::runtime_error("cannot write generation manifest");
    fs::rename(temporary, final_path);
  }
  const fs::path pointer = directory / ".CURRENT.tmp";
  { std::ofstream output(pointer, std::ios::trunc); output << generation.id << '\n'; }
  fs::rename(pointer, directory / "CURRENT");
}

Generation ManifestStore::load(const std::string& repository, const std::string& generation) const {
  if (!valid_name(repository) || !valid_name(generation)) throw std::invalid_argument("invalid manifest name");
  std::ifstream input(root_ / repository / "generations" / (generation + ".jsonl"));
  if (!input) throw std::runtime_error("manifest not found");
  std::string line;
  if (!std::getline(input, line)) throw std::runtime_error("empty manifest");
  Generation result{field(line, "repository"), field(line, "generation"), {}};
  if (result.repository != repository || result.id != generation || field(line, "type") != "osdd.manifest.v1") {
    throw std::runtime_error("manifest identity mismatch");
  }
  while (std::getline(input, line)) {
    if (line.empty()) continue;
    if (field(line, "type") != "mapping") throw std::runtime_error("unknown manifest record");
    Mapping item{field(line, "path"), field(line, "digest"), field(line, "component"),
                 field(line, "mediaType"), field(line, "integrity")};
    if (!valid_logical_path(item.path) || !Cas::valid_digest(item.digest) || !cas_.contains(item.digest)) {
      throw std::runtime_error("invalid or unavailable manifest mapping");
    }
    result.mappings.push_back(std::move(item));
  }
  return result;
}

std::optional<std::string> ManifestStore::current(const std::string& repository) const {
  if (!valid_name(repository)) return std::nullopt;
  std::ifstream input(root_ / repository / "CURRENT");
  std::string value;
  if (!(input >> value) || !valid_name(value)) return std::nullopt;
  return value;
}

std::vector<std::string> ManifestStore::repositories() const {
  std::vector<std::string> result;
  if (!fs::exists(root_)) return result;
  for (const auto& entry : fs::directory_iterator(root_)) {
    if (entry.is_directory() && valid_name(entry.path().filename().string()) &&
        current(entry.path().filename().string())) result.push_back(entry.path().filename().string());
  }
  std::sort(result.begin(), result.end());
  return result;
}

}  // namespace osdd
