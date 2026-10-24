#include "archipelago/ApbsArchive.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <zlib.h>

#include "archipelago/LevelIdentity.hpp"

namespace Archipelago {

    namespace {
        constexpr uint32_t endOfCentralDirectorySignature = 0x06054b50;
        constexpr uint32_t centralDirectorySignature = 0x02014b50;
        constexpr uint32_t localHeaderSignature = 0x04034b50;
        constexpr uint16_t methodStored = 0;
        constexpr uint16_t methodDeflate = 8;
        constexpr size_t endOfCentralDirectorySize = 22;
        constexpr size_t centralDirectoryHeaderSize = 46;
        constexpr size_t localHeaderSize = 30;

        uint16_t ReadUint16(std::string const& data, size_t offset) {
            if (offset + 2 > data.size())
                throw std::runtime_error("truncated archive");
            return static_cast<uint16_t>(static_cast<uint8_t>(data[offset]) | static_cast<uint8_t>(data[offset + 1]) << 8);
        }

        uint32_t ReadUint32(std::string const& data, size_t offset) {
            return ReadUint16(data, offset) | static_cast<uint32_t>(ReadUint16(data, offset + 2)) << 16;
        }

        std::string Inflate(std::string_view compressed, size_t expectedSize) {
            if (expectedSize == 0)
                return "";
            std::string output(expectedSize, '\0');
            z_stream stream{};
            if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
                throw std::runtime_error("could not start decompression");
            stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
            stream.avail_in = static_cast<uInt>(compressed.size());
            stream.next_out = reinterpret_cast<Bytef*>(output.data());
            stream.avail_out = static_cast<uInt>(output.size());
            int result = inflate(&stream, Z_FINISH);
            inflateEnd(&stream);
            if (result != Z_STREAM_END)
                throw std::runtime_error("corrupt compressed entry");
            return output;
        }

        size_t FindEndOfCentralDirectory(std::string const& archive) {
            if (archive.size() < endOfCentralDirectorySize)
                throw std::runtime_error("file is too small to be an archive");
            for (size_t offset = archive.size() - endOfCentralDirectorySize + 1; offset-- > 0;) {
                if (ReadUint32(archive, offset) == endOfCentralDirectorySignature)
                    return offset;
                if (archive.size() - offset > 0xFFFF + endOfCentralDirectorySize)
                    break;
            }
            throw std::runtime_error("not a zip archive");
        }
    }

    std::vector<std::string> ExtractPlaylistsFromApbs(std::string const& apbsPath, std::string const& outputDirectory) {
        std::ifstream stream(apbsPath, std::ios::binary);
        if (!stream)
            throw std::runtime_error("cannot open " + apbsPath);
        std::stringstream contents;
        contents << stream.rdbuf();
        std::string archive = contents.str();

        size_t directoryEnd = FindEndOfCentralDirectory(archive);
        uint16_t entryCount = ReadUint16(archive, directoryEnd + 10);
        size_t entryOffset = ReadUint32(archive, directoryEnd + 16);

        std::vector<std::string> writtenPaths;
        std::error_code ignoredError;
        std::filesystem::create_directories(outputDirectory, ignoredError);

        for (uint16_t entryIndex = 0; entryIndex < entryCount; entryIndex++) {
            if (ReadUint32(archive, entryOffset) != centralDirectorySignature)
                throw std::runtime_error("corrupt archive directory");
            uint16_t method = ReadUint16(archive, entryOffset + 10);
            uint32_t compressedSize = ReadUint32(archive, entryOffset + 20);
            uint32_t uncompressedSize = ReadUint32(archive, entryOffset + 24);
            uint16_t nameLength = ReadUint16(archive, entryOffset + 28);
            uint16_t extraLength = ReadUint16(archive, entryOffset + 30);
            uint16_t commentLength = ReadUint16(archive, entryOffset + 32);
            uint32_t localHeaderOffset = ReadUint32(archive, entryOffset + 42);
            if (entryOffset + centralDirectoryHeaderSize + nameLength > archive.size())
                throw std::runtime_error("corrupt archive directory");
            std::string entryName = archive.substr(entryOffset + centralDirectoryHeaderSize, nameLength);
            entryOffset += centralDirectoryHeaderSize + nameLength + extraLength + commentLength;

            // only the file name is kept so entries cannot escape the playlists folder
            std::string fileName = std::filesystem::path(entryName).filename().string();
            if (fileName.empty() || ToLower(std::filesystem::path(fileName).extension().string()) != ".bplist")
                continue;

            if (ReadUint32(archive, localHeaderOffset) != localHeaderSignature)
                throw std::runtime_error("corrupt archive entry " + entryName);
            size_t dataOffset = localHeaderOffset + localHeaderSize + ReadUint16(archive, localHeaderOffset + 26) + ReadUint16(archive, localHeaderOffset + 28);
            if (dataOffset + compressedSize > archive.size())
                throw std::runtime_error("truncated archive entry " + entryName);
            std::string_view compressed(archive.data() + dataOffset, compressedSize);

            std::string playlist;
            if (method == methodStored)
                playlist = std::string(compressed);
            else if (method == methodDeflate)
                playlist = Inflate(compressed, uncompressedSize);
            else
                throw std::runtime_error("unsupported compression in " + entryName);

            auto outputPath = (std::filesystem::path(outputDirectory) / fileName).string();
            std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
            output << playlist;
            if (!output)
                throw std::runtime_error("cannot write " + outputPath);
            writtenPaths.push_back(outputPath);
        }

        if (writtenPaths.empty())
            throw std::runtime_error("no .bplist found in " + apbsPath);
        return writtenPaths;
    }
}
