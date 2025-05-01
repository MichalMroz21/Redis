#include "rdb_file.hpp"
#include "redis_server.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <filesystem>

// Magic string for RDB file header
const std::string RDB_MAGIC_STRING = "REDIS0011";

// RDB file type flags
const uint8_t RDB_TYPE_STRING = 0;
const uint8_t RDB_EXPIRETIME_MS = 0xFC;
const uint8_t RDB_EXPIRETIME = 0xFD;
const uint8_t RDB_SELECTDB = 0xFE;
const uint8_t RDB_EOF = 0xFF;
const uint8_t RDB_METADATA = 0xFA;
const uint8_t RDB_HASH_TABLE_SIZE = 0xFB;

bool RdbFile::loadFromFile(const std::string& dir, const std::string& filename,
                          std::unordered_map<std::string, RedisValue>& data_store) {
    std::filesystem::path filePath = std::filesystem::path(dir) / filename;

    if (!std::filesystem::exists(filePath)) {
        std::cout << "RDB file does not exist: " << filePath << std::endl;
        return false;
    }

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "Failed to open RDB file: " << filePath << std::endl;
        return false;
    }

    if (!readHeader(file)) {
        std::cout << "Invalid RDB file header" << std::endl;
        return false;
    }

    skipMetadata(file);

    if (!readDatabase(file, data_store)) {
        std::cout << "Failed to read database section" << std::endl;
        return false;
    }

    file.close();

    return true;
}

bool RdbFile::saveToFile(const std::string& dir, const std::string& filename,
                        const std::unordered_map<std::string, RedisValue>& data_store) {

    std::filesystem::path filePath = std::filesystem::path(dir) / filename;
    std::filesystem::create_directories(filePath.parent_path());

    std::filesystem::path absolutePath = std::filesystem::absolute(filePath);
    std::cout << "Saving RDB file to: " << absolutePath << std::endl;

    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "Failed to open RDB file for writing: " << filePath << std::endl;
        return false;
    }

    writeHeader(file);
    writeMetadata(file);
    writeDatabase(file, data_store);
    writeEndOfFile(file);

    file.close();

    return true;
}

bool RdbFile::readHeader(std::ifstream& file) {
    char header[9];
    file.read(header, 9);

    return std::string(header, 9) == RDB_MAGIC_STRING;
}

void RdbFile::skipMetadata(std::ifstream& file) {
    uint8_t byte;

    while (file.read(reinterpret_cast<char*>(&byte), 1)) {
        if (byte != RDB_METADATA) {
            file.seekg(-1, std::ios::cur);
            break;
        }

        std::string key = readStringEncoding(file);
        std::string value = readStringEncoding(file);
    }
}

bool RdbFile::readDatabase(std::ifstream& file, std::unordered_map<std::string, RedisValue>& data_store) {
    uint8_t byte;

    while (file.read(reinterpret_cast<char*>(&byte), 1)) {
        if (byte == RDB_EOF) {
            break;
        } else if (byte == RDB_SELECTDB) {
            uint64_t dbIndex = readSizeEncoding(file);

            if (dbIndex != 0) {
                std::cout << "Unsupported database index: " << dbIndex << std::endl;
                return false;
            }
        } else if (byte == RDB_HASH_TABLE_SIZE) {
            readSizeEncoding(file);
            readSizeEncoding(file);
        } else if (byte == RDB_EXPIRETIME || byte == RDB_EXPIRETIME_MS) {
            std::chrono::steady_clock::time_point expiry;
            bool has_expiry = true;

            if (byte == RDB_EXPIRETIME) {
                uint32_t expirySeconds;
                file.read(reinterpret_cast<char*>(&expirySeconds), 4);

                auto now = std::chrono::steady_clock::now();
                auto systemNow = std::chrono::system_clock::now();
                auto expiryTime = std::chrono::system_clock::from_time_t(expirySeconds);

                auto diff = expiryTime - systemNow;
                expiry = now + diff;
            } else {
                uint64_t expiryMilliseconds;
                file.read(reinterpret_cast<char*>(&expiryMilliseconds), 8);

                auto now = std::chrono::steady_clock::now();
                auto systemNow = std::chrono::system_clock::now();
                auto expiryTime = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(expiryMilliseconds));

                auto diff = expiryTime - systemNow;
                expiry = now + diff;
            }

            uint8_t valueType;

            file.read(reinterpret_cast<char*>(&valueType), 1);

            if (valueType != RDB_TYPE_STRING) {
                std::cout << "Unsupported value type: " << static_cast<int>(valueType) << std::endl;
                return false;
            }

            std::string key = readStringEncoding(file);
            std::string value = readStringEncoding(file);

            data_store[key] = RedisValue(value, expiry, has_expiry);
        } else if (byte == RDB_TYPE_STRING) {

            std::string key = readStringEncoding(file);
            std::string value = readStringEncoding(file);

            data_store[key] = RedisValue(value);
        } else {
            std::cout << "Unsupported RDB file byte: " << static_cast<int>(byte) << std::endl;
            return false;
        }
    }

    return true;
}

void RdbFile::writeHeader(std::ofstream& file) {
    file.write(RDB_MAGIC_STRING.c_str(), RDB_MAGIC_STRING.size());
}

void RdbFile::writeMetadata(std::ofstream& file) {
    file.put(RDB_METADATA);

    writeStringEncoding(file, "redis-ver");
    writeStringEncoding(file, "6.0.16");

    file.put(RDB_METADATA);

    writeStringEncoding(file, "redis-bits");
    writeStringEncoding(file, "64");
}

void RdbFile::writeDatabase(std::ofstream& file, const std::unordered_map<std::string, RedisValue>& data_store) {
    file.put(RDB_SELECTDB);
    writeSizeEncoding(file, 0);

    file.put(RDB_HASH_TABLE_SIZE);
    writeSizeEncoding(file, data_store.size());

    size_t expiryCount = 0;

    for (const auto& [key, value] : data_store) {
        if (value.has_expiry) {
            expiryCount++;
        }
    }
    writeSizeEncoding(file, expiryCount);

    for (const auto& [key, value] : data_store) {
        if (value.has_expiry) {
            file.put(RDB_EXPIRETIME_MS);

            auto now = std::chrono::steady_clock::now();
            auto systemNow = std::chrono::system_clock::now();
            auto diff = value.expiry - now;
            auto expiryTime = systemNow + diff;
            auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                expiryTime.time_since_epoch()).count();

            file.write(reinterpret_cast<const char*>(&milliseconds), 8);
        }

        file.put(RDB_TYPE_STRING);

        writeStringEncoding(file, key);
        writeStringEncoding(file, value.value);
    }
}

void RdbFile::writeEndOfFile(std::ofstream& file) {
    file.put(RDB_EOF);

    uint64_t checksum = 0;
    file.write(reinterpret_cast<const char*>(&checksum), 8);
}

uint64_t RdbFile::readSizeEncoding(std::ifstream& file) {
    uint8_t byte;
    file.read(reinterpret_cast<char*>(&byte), 1);

    uint8_t firstTwoBits = (byte & 0xC0) >> 6;

    if (firstTwoBits == 0) {
        return byte & 0x3F;
    } else if (firstTwoBits == 1) {
        uint8_t nextByte;
        file.read(reinterpret_cast<char*>(&nextByte), 1);

        uint16_t size = ((byte & 0x3F) << 8) | nextByte;
        return size;
    } else if (firstTwoBits == 2) {
        uint32_t size;
        file.read(reinterpret_cast<char*>(&size), 4);

        // Convert from big-endian to host byte order
        size = ((size & 0xFF000000) >> 24) |
               ((size & 0x00FF0000) >> 8) |
               ((size & 0x0000FF00) << 8) |
               ((size & 0x000000FF) << 24);

        return size;
    } else {
        if (byte == 0xC0) {
            uint8_t intValue;
            file.read(reinterpret_cast<char*>(&intValue), 1);
            return intValue;
        } else if (byte == 0xC1) {
            uint16_t intValue;
            file.read(reinterpret_cast<char*>(&intValue), 2);

            // Convert from little-endian to host byte order
            intValue = ((intValue & 0xFF00) >> 8) |
                       ((intValue & 0x00FF) << 8);

            return intValue;
        } else if (byte == 0xC2) {
            uint32_t intValue;
            file.read(reinterpret_cast<char*>(&intValue), 4);

            // Convert from little-endian to host byte order
            intValue = ((intValue & 0xFF000000) >> 24) |
                       ((intValue & 0x00FF0000) >> 8) |
                       ((intValue & 0x0000FF00) << 8) |
                       ((intValue & 0x000000FF) << 24);

            return intValue;
        } else {
            std::cout << "Unsupported string encoding: " << static_cast<int>(byte) << std::endl;
            return 0;
        }
    }
}

std::string RdbFile::readStringEncoding(std::ifstream& file) {
    uint64_t size = readSizeEncoding(file);

    std::string str(size, '\0');
    file.read(&str[0], size);

    return str;
}

void RdbFile::writeSizeEncoding(std::ofstream& file, uint64_t size) {
    if (size < 64) {
        // Size fits in 6 bits
        uint8_t byte = size & 0x3F;
        file.put(byte);
    } else if (size < 16384) {
        // Size fits in 14 bits
        uint16_t twoBytes = size | 0x4000; // Set the first two bits to 01

        // Write in big-endian order
        file.put((twoBytes >> 8) & 0xFF);
        file.put(twoBytes & 0xFF);
    } else {
        // Size requires 4 bytes
        file.put(0x80); // First two bits are 10, rest are ignored

        // Write in big-endian order
        file.put((size >> 24) & 0xFF);
        file.put((size >> 16) & 0xFF);
        file.put((size >> 8) & 0xFF);
        file.put(size & 0xFF);
    }
}

void RdbFile::writeStringEncoding(std::ofstream& file, const std::string& str) {
    writeSizeEncoding(file, str.size());
    file.write(str.c_str(), str.size());
}

uint64_t RdbFile::calculateCRC64(const std::string& data) {
    // Simplified CRC64 calculation (returns 0 for now)
    return 0;
}
