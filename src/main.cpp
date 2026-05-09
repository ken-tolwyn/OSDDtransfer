#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct MavenConfig {
    bool enabled = false;
    std::string bindAddress = "0.0.0.0";
    int port = 8081;
    fs::path cacheDirectory;
};

struct ImageConfig {
    bool enabled = false;
    fs::path scopeFile;
    fs::path zotStorageDirectory;
};

struct RpmSyncConfig {
    bool enabled = false;
    fs::path repoDefinitionDirectory;
    fs::path outputDirectory;
    std::string reposyncBinary = "reposync";
    bool downloadMetadata = true;
};

struct PathsConfig {
    fs::path stagingDirectory;
    fs::path destinationDirectory;
};

struct AppConfig {
    std::string mode = "upstream";
    MavenConfig maven;
    ImageConfig images;
    RpmSyncConfig rpm;
    PathsConfig paths;
};

class Shell {
public:
    static void run(const std::string& cmd) {
        std::cout << "[exec] " << cmd << '\n';
        int rc = std::system(cmd.c_str());
        if (rc != 0) {
            throw std::runtime_error("command failed with exit code " + std::to_string(rc) + ": " + cmd);
        }
    }
};

static std::string quote(const fs::path& p) {
    return "\"" + p.string() + "\"";
}

class TransferManager {
public:
    explicit TransferManager(AppConfig config) : config_(std::move(config)) {}

    void run() {
        validatePaths();

        if (config_.mode == "upstream") {
            if (config_.maven.enabled) {
                runMavenCache();
            }
            if (config_.images.enabled) {
                syncImagesToZotDir();
            }
            if (config_.rpm.enabled) {
                syncRpmRepos();
            }
            promoteStagedChanges();
        } else {
            std::cout << "running in downstream mode: only promote staged content to destination\n";
            promoteStagedChanges();
        }
    }

private:
    AppConfig config_;
    std::set<fs::path> touchedFiles_;

    void validatePaths() {
        if (config_.paths.stagingDirectory.empty() || config_.paths.destinationDirectory.empty()) {
            throw std::runtime_error("paths.staging_directory and paths.destination_directory are required");
        }
        fs::create_directories(config_.paths.stagingDirectory);
        fs::create_directories(config_.paths.destinationDirectory);
    }

    void runMavenCache() {
        fs::create_directories(config_.maven.cacheDirectory);
        fs::path nginxConf = config_.paths.stagingDirectory / "runtime" / "nginx-maven-cache.conf";
        fs::create_directories(nginxConf.parent_path());

        std::ofstream out(nginxConf);
        out << "events {}\n"
               "http {\n"
               "  proxy_cache_path " << config_.maven.cacheDirectory.string()
            << " levels=1:2 keys_zone=maven_cache:100m max_size=50g inactive=7d use_temp_path=off;\n"
               "  server {\n"
               "    listen " << config_.maven.bindAddress << ':' << config_.maven.port << ";\n"
               "    location / {\n"
               "      proxy_cache maven_cache;\n"
               "      proxy_pass https://repo1.maven.org/maven2/;\n"
               "      proxy_set_header Host repo1.maven.org;\n"
               "    }\n"
               "  }\n"
               "}\n";
        out.close();

        touchedFiles_.insert(fs::relative(nginxConf, config_.paths.stagingDirectory));
        std::cout << "Maven cache config staged at " << nginxConf << '\n';
    }

    void syncImagesToZotDir() {
        fs::create_directories(config_.images.zotStorageDirectory);
        std::ifstream in(config_.images.scopeFile);
        if (!in) {
            throw std::runtime_error("unable to read image scope file: " + config_.images.scopeFile.string());
        }

        std::string image;
        while (std::getline(in, image)) {
            if (image.empty() || image[0] == '#') {
                continue;
            }
            std::string cmd = "skopeo copy --all docker://" + image + " dir:" + quote(config_.images.zotStorageDirectory / sanitizeImage(image));
            Shell::run(cmd);
            touchedFiles_.insert(fs::path("images") / sanitizeImage(image));
        }
    }

    void syncRpmRepos() {
        fs::create_directories(config_.rpm.outputDirectory);
        std::vector<std::string> repoIds;

        for (const auto& entry : fs::directory_iterator(config_.rpm.repoDefinitionDirectory)) {
            if (entry.path().extension() == ".repo") {
                repoIds.push_back(entry.path().stem().string());
            }
        }

        for (const auto& repo : repoIds) {
            std::ostringstream cmd;
            cmd << config_.rpm.reposyncBinary
                << " --repoid=" << repo
                << " --download-path=" << quote(config_.rpm.outputDirectory);
            if (config_.rpm.downloadMetadata) {
                cmd << " --download-metadata";
            }
            Shell::run(cmd.str());
            touchedFiles_.insert(fs::path("rpm") / repo);
        }
    }

    void promoteStagedChanges() {
        if (touchedFiles_.empty()) {
            std::cout << "No staged files were tracked in this run.\n";
            return;
        }

        for (const auto& rel : touchedFiles_) {
            const fs::path src = config_.paths.stagingDirectory / rel;
            const fs::path dst = config_.paths.destinationDirectory / rel;
            fs::create_directories(dst.parent_path());

            if (fs::is_directory(src)) {
                Shell::run("cp -lnr " + quote(src) + " " + quote(dst.parent_path()));
                continue;
            }

            std::string cmd = "cp -ln " + quote(src) + " " + quote(dst);
            int rc = std::system(cmd.c_str());
            if (rc != 0) {
                fs::copy_file(src, dst, fs::copy_options::overwrite_existing);
            }
        }
    }

    static std::string sanitizeImage(const std::string& image) {
        std::string out = image;
        for (char& c : out) {
            if (c == '/' || c == ':' || c == '@') {
                c = '_';
            }
        }
        return out;
    }
};

AppConfig loadConfig(const fs::path& path) {
    YAML::Node root = YAML::LoadFile(path.string());
    AppConfig cfg;

    cfg.mode = root["mode"] ? root["mode"].as<std::string>() : "upstream";

    if (auto maven = root["maven"]) {
        cfg.maven.enabled = maven["enabled"] && maven["enabled"].as<bool>();
        cfg.maven.bindAddress = maven["bind_address"] ? maven["bind_address"].as<std::string>() : "0.0.0.0";
        cfg.maven.port = maven["port"] ? maven["port"].as<int>() : 8081;
        cfg.maven.cacheDirectory = maven["cache_directory"] ? maven["cache_directory"].as<std::string>() : "";
    }

    if (auto images = root["images"]) {
        cfg.images.enabled = images["enabled"] && images["enabled"].as<bool>();
        cfg.images.scopeFile = images["scope_file"] ? images["scope_file"].as<std::string>() : "";
        cfg.images.zotStorageDirectory = images["zot_storage_directory"] ? images["zot_storage_directory"].as<std::string>() : "";
    }

    if (auto rpm = root["rpm"]) {
        cfg.rpm.enabled = rpm["enabled"] && rpm["enabled"].as<bool>();
        cfg.rpm.repoDefinitionDirectory = rpm["repo_definition_directory"] ? rpm["repo_definition_directory"].as<std::string>() : "";
        cfg.rpm.outputDirectory = rpm["output_directory"] ? rpm["output_directory"].as<std::string>() : "";
        cfg.rpm.reposyncBinary = rpm["reposync_binary"] ? rpm["reposync_binary"].as<std::string>() : "reposync";
        cfg.rpm.downloadMetadata = !rpm["download_metadata"] || rpm["download_metadata"].as<bool>();
    }

    if (auto paths = root["paths"]) {
        cfg.paths.stagingDirectory = paths["staging_directory"] ? paths["staging_directory"].as<std::string>() : "";
        cfg.paths.destinationDirectory = paths["destination_directory"] ? paths["destination_directory"].as<std::string>() : "";
    }

    return cfg;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: osddtransfer <config.yaml>\n";
        return 2;
    }

    try {
        AppConfig cfg = loadConfig(argv[1]);
        TransferManager manager(std::move(cfg));
        manager.run();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "error: " << ex.what() << '\n';
        return 1;
    }
}
