#include "osdd/cas.hpp"

#include <openssl/evp.h>

#include <array>
#include <fstream>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace osdd {
namespace {
struct EvpDeleter {
  void operator()(EVP_MD_CTX* value) const { EVP_MD_CTX_free(value); }
};
}  // namespace

Cas::Cas(fs::path root) : root_(std::move(root)) {
  fs::create_directories(root_ / "sha256");
}

bool Cas::valid_digest(const std::string& digest) {
  if (digest.size() != 64) return false;
  for (const char ch : digest) {
    if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) return false;
  }
  return true;
}

fs::path Cas::object_path(const std::string& digest) const {
  if (!valid_digest(digest)) throw std::invalid_argument("invalid SHA-256 digest");
  return root_ / "sha256" / digest.substr(0, 2) / digest;
}

bool Cas::contains(const std::string& digest) const {
  if (!valid_digest(digest)) return false;
  std::error_code error;
  return fs::is_regular_file(object_path(digest), error);
}

std::string Cas::put(std::istream& input) {
  const fs::path staging = root_ / "staging";
  fs::create_directories(staging);
  const fs::path temporary = staging / ("object-" + std::to_string(std::random_device{}()));
  std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
  if (!output) throw std::runtime_error("cannot create CAS staging file");

  std::unique_ptr<EVP_MD_CTX, EvpDeleter> context(EVP_MD_CTX_new());
  if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) {
    throw std::runtime_error("cannot initialize SHA-256");
  }
  std::array<char, 64 * 1024> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = input.gcount();
    if (count > 0) {
      output.write(buffer.data(), count);
      if (!output || EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(count)) != 1) {
        fs::remove(temporary);
        throw std::runtime_error("failed while ingesting CAS object");
      }
    }
  }
  if (!input.eof()) {
    fs::remove(temporary);
    throw std::runtime_error("failed to read CAS input");
  }
  output.close();

  std::array<unsigned char, EVP_MAX_MD_SIZE> hash{};
  unsigned int length = 0;
  if (EVP_DigestFinal_ex(context.get(), hash.data(), &length) != 1 || length != 32) {
    fs::remove(temporary);
    throw std::runtime_error("cannot finalize SHA-256");
  }
  std::ostringstream encoded;
  for (unsigned int i = 0; i < length; ++i) {
    encoded << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(hash[i]);
  }
  const std::string digest = encoded.str();
  const fs::path destination = object_path(digest);
  fs::create_directories(destination.parent_path());
  if (fs::exists(destination)) {
    fs::remove(temporary);
  } else {
    std::error_code error;
    fs::rename(temporary, destination, error);
    if (error) {
      if (fs::exists(destination)) fs::remove(temporary);
      else throw std::runtime_error("cannot commit CAS object: " + error.message());
    }
  }
  return digest;
}

}  // namespace osdd
