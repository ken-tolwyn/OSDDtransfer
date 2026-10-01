#include "osdd/http_server.hpp"

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace osdd {
namespace {
std::string decode(std::string_view value) {
  std::string result;
  auto hex = [](char ch) -> int {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
  };
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '%' && i + 2 < value.size()) {
      const int high = hex(value[i + 1]);
      const int low = hex(value[i + 2]);
      if (high < 0 || low < 0) throw std::invalid_argument("bad URL encoding");
      result.push_back(static_cast<char>((high << 4) | low));
      i += 2;
    } else result.push_back(value[i] == '+' ? ' ' : value[i]);
  }
  return result;
}

void send_response(int client, int status, std::string_view type, const std::string& body,
                   std::string_view extra = {}) {
  const std::string reason = status == 200 ? "OK" : status == 404 ? "Not Found" : "Bad Request";
  const std::string header = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\nContent-Type: " +
                             std::string(type) + "\r\nContent-Length: " + std::to_string(body.size()) +
                             "\r\nConnection: close\r\n" + std::string(extra) + "\r\n";
  ::send(client, header.data(), header.size(), MSG_NOSIGNAL);
  ::send(client, body.data(), body.size(), MSG_NOSIGNAL);
}
}  // namespace

HttpServer::HttpServer(const Cas& cas, const Index& index) : cas_(cas), index_(index) {}

void HttpServer::run(const std::uint16_t port) const {
  const int server = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server < 0) throw std::runtime_error("cannot create HTTP socket");
  int reuse = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(port);
  if (::bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || ::listen(server, 64) < 0) {
    ::close(server);
    throw std::runtime_error("cannot bind HTTP server");
  }
  while (true) {
    const int client = ::accept(server, nullptr, nullptr);
    if (client < 0) continue;
    std::array<char, 8192> buffer{};
    const auto count = ::recv(client, buffer.data(), buffer.size() - 1, 0);
    try {
      if (count <= 0) throw std::invalid_argument("empty request");
      const std::string request(buffer.data(), static_cast<std::size_t>(count));
      const auto first_space = request.find(' ');
      const auto second_space = request.find(' ', first_space + 1);
      if (request.substr(0, first_space) != "GET" || second_space == std::string::npos) throw std::invalid_argument("only GET is supported");
      const std::string target = request.substr(first_space + 1, second_space - first_space - 1);
      if (target == "/healthz") send_response(client, 200, "application/json", "{\"status\":\"ok\"}\n");
      else if (target.starts_with("/cas/sha256/")) {
        const std::string digest = target.substr(12);
        if (!cas_.contains(digest)) send_response(client, 404, "application/json", "{\"error\":\"not found\"}\n");
        else {
          std::ifstream input(cas_.object_path(digest), std::ios::binary);
          const std::string body((std::istreambuf_iterator<char>(input)), {});
          send_response(client, 200, "application/octet-stream", body);
        }
      } else if (target.starts_with("/api/v1/repositories/")) {
        const std::string rest = target.substr(21);
        const auto separator = rest.find('/');
        if (separator == std::string::npos) throw std::invalid_argument("incomplete API path");
        const std::string repository = decode(rest.substr(0, separator));
        const std::string operation = rest.substr(separator + 1);
        if (operation.starts_with("paths/")) {
          const auto mapping = index_.resolve(repository, decode(operation.substr(6)));
          if (!mapping) send_response(client, 404, "application/json", "{\"error\":\"not found\"}\n");
          else send_response(client, 200, "application/json", "{\"digest\":\"" + mapping->digest + "\",\"component\":\"" + mapping->component + "\",\"mediaType\":\"" + mapping->media_type + "\",\"integrity\":\"" + mapping->integrity + "\"}\n");
        } else if (operation.starts_with("sbom?format=")) {
          const std::string format = operation.substr(12);
          send_response(client, 200, format == "spdx" ? "application/spdx+json" : "application/vnd.cyclonedx+json", index_.sbom(repository, format));
        } else send_response(client, 404, "application/json", "{\"error\":\"not found\"}\n");
      } else send_response(client, 404, "application/json", "{\"error\":\"not found\"}\n");
    } catch (const std::exception& error) {
      send_response(client, 400, "application/json", "{\"error\":\"" + std::string(error.what()) + "\"}\n");
    }
    ::close(client);
  }
}

}  // namespace osdd
