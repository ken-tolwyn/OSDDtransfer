#pragma once

#include "osdd/cas.hpp"
#include "osdd/index.hpp"

#include <cstdint>

namespace osdd {

class HttpServer {
 public:
  HttpServer(const Cas& cas, const Index& index);
  void run(std::uint16_t port) const;

 private:
  const Cas& cas_;
  const Index& index_;
};

}  // namespace osdd
