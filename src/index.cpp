#include "osdd/index.hpp"

#include <sqlite3.h>

#include <map>
#include <stdexcept>

namespace osdd {
namespace {
void check(const int code, sqlite3* db, const char* operation) {
  if (code != SQLITE_OK && code != SQLITE_DONE && code != SQLITE_ROW) {
    throw std::runtime_error(std::string(operation) + ": " + sqlite3_errmsg(db));
  }
}

void execute(sqlite3* db, const char* sql) {
  char* error = nullptr;
  const int code = sqlite3_exec(db, sql, nullptr, nullptr, &error);
  if (code != SQLITE_OK) {
    const std::string message = error ? error : "SQLite error";
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

std::string json_escape(const std::string& value) {
  std::string output;
  for (const char ch : value) {
    if (ch == '"' || ch == '\\') output.push_back('\\');
    if (ch == '\n') output += "\\n";
    else output.push_back(ch);
  }
  return output;
}
}  // namespace

Index::Index(std::filesystem::path path) {
  if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
  if (sqlite3_open(path.string().c_str(), &db_) != SQLITE_OK) {
    const std::string message = db_ ? sqlite3_errmsg(db_) : "unknown error";
    if (db_) sqlite3_close(db_);
    db_ = nullptr;
    throw std::runtime_error("cannot open index: " + message);
  }
  execute(db_, "PRAGMA foreign_keys=ON; PRAGMA journal_mode=WAL;");
}

Index::~Index() { if (db_) sqlite3_close(db_); }

void Index::rebuild(const ManifestStore& manifests) {
  execute(db_, "BEGIN IMMEDIATE; DROP TABLE IF EXISTS mappings; DROP TABLE IF EXISTS generations;"
               "CREATE TABLE generations(repository TEXT PRIMARY KEY, generation TEXT NOT NULL);"
               "CREATE TABLE mappings(repository TEXT NOT NULL, path TEXT NOT NULL, digest TEXT NOT NULL,"
               "component TEXT NOT NULL, media_type TEXT NOT NULL, integrity TEXT NOT NULL,"
               "PRIMARY KEY(repository,path), FOREIGN KEY(repository) REFERENCES generations(repository));");
  sqlite3_stmt* generation_statement = nullptr;
  sqlite3_stmt* mapping_statement = nullptr;
  try {
    check(sqlite3_prepare_v2(db_, "INSERT INTO generations VALUES(?,?)", -1, &generation_statement, nullptr), db_, "prepare generation");
    check(sqlite3_prepare_v2(db_, "INSERT INTO mappings VALUES(?,?,?,?,?,?)", -1, &mapping_statement, nullptr), db_, "prepare mapping");
    for (const auto& repository : manifests.repositories()) {
      const auto current = manifests.current(repository);
      if (!current) continue;
      const Generation generation = manifests.load(repository, *current);
      sqlite3_bind_text(generation_statement, 1, repository.c_str(), -1, SQLITE_TRANSIENT);
      sqlite3_bind_text(generation_statement, 2, current->c_str(), -1, SQLITE_TRANSIENT);
      check(sqlite3_step(generation_statement), db_, "insert generation");
      sqlite3_reset(generation_statement);
      for (const auto& item : generation.mappings) {
        const std::string* values[] = {&repository, &item.path, &item.digest, &item.component,
                                       &item.media_type, &item.integrity};
        for (int index = 0; index < 6; ++index) sqlite3_bind_text(mapping_statement, index + 1, values[index]->c_str(), -1, SQLITE_TRANSIENT);
        check(sqlite3_step(mapping_statement), db_, "insert mapping");
        sqlite3_reset(mapping_statement);
      }
    }
    sqlite3_finalize(mapping_statement);
    sqlite3_finalize(generation_statement);
    execute(db_, "COMMIT;");
  } catch (...) {
    if (mapping_statement) sqlite3_finalize(mapping_statement);
    if (generation_statement) sqlite3_finalize(generation_statement);
    sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
    throw;
  }
}

std::optional<Mapping> Index::resolve(const std::string& repository, const std::string& path) const {
  sqlite3_stmt* statement = nullptr;
  check(sqlite3_prepare_v2(db_, "SELECT path,digest,component,media_type,integrity FROM mappings WHERE repository=? AND path=?", -1, &statement, nullptr), db_, "prepare resolve");
  sqlite3_bind_text(statement, 1, repository.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, path.c_str(), -1, SQLITE_TRANSIENT);
  const int result = sqlite3_step(statement);
  std::optional<Mapping> mapping;
  if (result == SQLITE_ROW) {
    auto text = [statement](int column) { return std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, column))); };
    mapping = Mapping{text(0), text(1), text(2), text(3), text(4)};
  } else if (result != SQLITE_DONE) {
    sqlite3_finalize(statement);
    check(result, db_, "resolve");
  }
  sqlite3_finalize(statement);
  return mapping;
}

std::string Index::sbom(const std::string& repository, const std::string& format) const {
  if (format != "cyclonedx" && format != "spdx") throw std::invalid_argument("format must be cyclonedx or spdx");
  sqlite3_stmt* statement = nullptr;
  check(sqlite3_prepare_v2(db_, "SELECT component, MIN(path) FROM mappings WHERE repository=? GROUP BY component ORDER BY component", -1, &statement, nullptr), db_, "prepare SBOM");
  sqlite3_bind_text(statement, 1, repository.c_str(), -1, SQLITE_TRANSIENT);
  std::vector<std::string> components;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const auto* component = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
    const auto* path = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
    components.emplace_back(component && *component ? component : path);
  }
  sqlite3_finalize(statement);
  std::string output;
  if (format == "cyclonedx") {
    output = "{\"bomFormat\":\"CycloneDX\",\"specVersion\":\"1.5\",\"version\":1,\"components\":[";
    for (std::size_t i = 0; i < components.size(); ++i) {
      if (i) output += ',';
      output += "{\"type\":\"library\",\"bom-ref\":\"" + json_escape(components[i]) + "\",\"name\":\"" + json_escape(components[i]) + "\"}";
    }
    return output + "]}\n";
  }
  output = "{\"spdxVersion\":\"SPDX-2.3\",\"dataLicense\":\"CC0-1.0\",\"SPDXID\":\"SPDXRef-DOCUMENT\",\"name\":\"" + json_escape(repository) + "\",\"packages\":[";
  for (std::size_t i = 0; i < components.size(); ++i) {
    if (i) output += ',';
    output += "{\"name\":\"" + json_escape(components[i]) + "\",\"SPDXID\":\"SPDXRef-Package-" + std::to_string(i + 1) + "\",\"downloadLocation\":\"NOASSERTION\"}";
  }
  return output + "]}\n";
}

}  // namespace osdd
