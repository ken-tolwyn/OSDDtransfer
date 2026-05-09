#include "runtime.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace upstreamd {

void write_zot_runtime_config(const Config& config) {
  if (!config.zot.enabled) {
    return;
  }

  std::error_code error;
  std::filesystem::create_directories(config.zot.runtime_config.parent_path(),
                                      error);
  if (error) {
    throw std::runtime_error("failed to prepare Zot config directory: " +
                             error.message());
  }

  std::ofstream output(config.zot.runtime_config);
  if (!output) {
    throw std::runtime_error("unable to write Zot runtime config: " +
                             config.zot.runtime_config.string());
  }

  output << "{\n"
         << "  \"distSpecVersion\": \"1.1.1\",\n"
         << "  \"storage\": {\n"
         << "    \"rootDirectory\": \"" << config.zot.data_dir.string()
         << "\"\n"
         << "  },\n"
         << "  \"http\": {\n"
         << "    \"address\": \"" << config.zot.listen_host << "\",\n"
         << "    \"port\": \"" << config.zot.listen_port << "\"\n"
         << "  },\n"
         << "  \"log\": {\n"
         << "    \"level\": \"" << config.zot.log_level << "\"\n"
         << "  }\n"
         << "}\n";
}

}  // namespace upstreamd
