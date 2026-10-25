#include "archipelago/CertificateStore.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace Archipelago {

    namespace {
        constexpr std::string_view certificateBegin = "-----BEGIN CERTIFICATE-----";
        constexpr std::string_view certificateEnd = "-----END CERTIFICATE-----";

        // Android store files carry a human readable dump after the PEM block
        int AppendPemBlocks(std::string const& fileContents, std::string& bundle) {
            int blockCount = 0;
            size_t searchFrom = 0;
            while (true) {
                auto begin = fileContents.find(certificateBegin, searchFrom);
                if (begin == std::string::npos)
                    break;
                auto end = fileContents.find(certificateEnd, begin);
                if (end == std::string::npos)
                    break;
                end += certificateEnd.size();
                bundle.append(fileContents, begin, end - begin);
                bundle += '\n';
                searchFrom = end;
                blockCount++;
            }
            return blockCount;
        }

        std::string ReadFile(std::filesystem::path const& path) {
            std::ifstream stream(path, std::ios::binary);
            std::stringstream contents;
            contents << stream.rdbuf();
            return contents.str();
        }
    }

    std::string WriteSystemCertificateBundle(std::string const& bundlePath) {
        // newer Android releases ship updatable roots in the conscrypt apex
        std::filesystem::path const certificateDirectories[] = {
            "/apex/com.android.conscrypt/cacerts",
            "/system/etc/security/cacerts",
        };
        std::filesystem::path const certificateBundles[] = {
            "/etc/ssl/certs/ca-certificates.crt",
            "/etc/pki/tls/certs/ca-bundle.crt",
        };

        std::string bundle;
        int certificateCount = 0;
        std::error_code ignoredError;

        for (auto const& directory : certificateDirectories) {
            if (!std::filesystem::is_directory(directory, ignoredError))
                continue;
            for (auto const& entry : std::filesystem::directory_iterator(directory, ignoredError)) {
                if (entry.is_regular_file(ignoredError))
                    certificateCount += AppendPemBlocks(ReadFile(entry.path()), bundle);
            }
            if (certificateCount > 0)
                break;
        }

        if (certificateCount == 0) {
            for (auto const& bundleFile : certificateBundles) {
                if (std::filesystem::is_regular_file(bundleFile, ignoredError))
                    certificateCount += AppendPemBlocks(ReadFile(bundleFile), bundle);
                if (certificateCount > 0)
                    break;
            }
        }

        if (certificateCount == 0)
            return "";

        std::filesystem::create_directories(std::filesystem::path(bundlePath).parent_path(), ignoredError);
        std::ofstream output(bundlePath, std::ios::binary | std::ios::trunc);
        output << bundle;
        return output ? bundlePath : "";
    }
}
