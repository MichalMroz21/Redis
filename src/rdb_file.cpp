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
    // Construct the full path to the RDB file
    std::filesystem::path filePath = std::filesystem::path(dir) / filename;

    // Get the absolute path and print it
    std::filesystem::path absolutePath = std::filesystem::absolute(filePath);
    std::cout << "Loading RDB file from: " << absolutePath << std::endl;

    // Check if the file exists
    if (!std::filesystem::exists(filePath)) {
        std::cout << "RDB file does not exist: " << absolutePath << std::endl;
        return false;
    }

    // Open the file for reading in binary mode
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "Failed to open RDB file: " << absolutePath << std::endl;
        return false;
    }

    std::cout << "Successfully opened RDB file" << std::endl;

    // Read the header
    if (!readHeader(file)) {
        std::cout << "Invalid RDB file header" << std::endl;
        return false;
    }

    std::cout << "Valid RDB file header" << std::endl;

    // Skip metadata section
    skipMetadata(file);

    std::cout << "Skipped metadata section" << std::endl;

    // Read database section
    if (!readDatabase(file, data_store)) {
        std::cout << "Failed to read database section" << std::endl;
        return false;
    }

    std::cout << "Successfully read database section" << std::endl;
    std::cout << "Loaded " << data_store.size() << " keys from RDB file" << std::endl;

    // Print all loaded keys
    for (const auto& [key, value] : data_store) {
        std::cout << "Loaded key: '" << key << "' with value: '" << value.value << "'" << std::endl;
    }

    // Close the file
    file.close();

    return true;
}

bool RdbFile::saveToFile(const std::string& dir, const std::string& filename,
                        const std::unordered_map<std::string, RedisValue>& data_store) {
    // Construct the full path to the RDB file
    std::filesystem::path filePath = std::filesystem::path(dir) / filename;

    // Get the absolute path and print it
    std::filesystem::path absolutePath = std::filesystem::absolute(filePath);
    std::cout << "Saving RDB file to: " << absolutePath << std::endl;

    // Create directory if it doesn't exist
    std::filesystem::create_directories(filePath.parent_path());

    // Open the file for writing in binary mode
    std::ofstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "Failed to open RDB file for writing: " << absolutePath << std::endl;
        return false;
    }

    // Write the header
    writeHeader(file);

    // Write metadata section
    writeMetadata(file);

    // Write database section
    writeDatabase(file, data_store);

    // Write end of file section
    writeEndOfFile(file);

    // Close the file
    file.close();

    return true;
}

bool RdbFile::readHeader(std::ifstream& file) {
    char header[9];
    file.read(header, 9);

    // Check if the header matches the magic string
    std::string headerStr(header, 9);
    std::cout << "Read header: " << headerStr << std::endl;
    return headerStr == RDB_MAGIC_STRING;
}

void RdbFile::skipMetadata(std::ifstream& file) {
    uint8_t byte;

    // Read bytes until we find a non-metadata byte
    while (file.read(reinterpret_cast<char*>(&byte), 1)) {
        if (byte != RDB_METADATA) {
            // Go back one byte since we've read past the metadata section
            file.seekg(-1, std::ios::cur);
            break;
        }

        // Skip the metadata key
        std::string key = readStringEncoding(file);
        std::cout << "Skipping metadata key: " << key << std::endl;

        // Skip the metadata value
        std::string value = readStringEncoding(file);
        std::cout << "Skipping metadata value: " << value << std::endl;
    }
}

bool RdbFile::readDatabase(std::ifstream& file, std::unordered_map<std::string, RedisValue>& data_store) {
    uint8_t byte;

    while (file.read(reinterpret_cast<char*>(&byte), 1)) {
        std::cout << "Read byte: 0x" << std::hex << static_cast<int>(byte) << std::dec << std::endl;

        if (byte == RDB_EOF) {
            // End of file, we're done
            std::cout << "End of file marker found" << std::endl;
            break;
        } else if (byte == RDB_SELECTDB) {
            // Database selector, read the database index
            uint64_t dbIndex = readSizeEncoding(file);
            std::cout << "Selected database: " << dbIndex << std::endl;

            // For now, we only support database 0
            if (dbIndex != 0) {
                std::cout << "Unsupported database index: " << dbIndex << std::endl;
                return false;
            }
        } else if (byte == RDB_HASH_TABLE_SIZE) {
            // Hash table size information, skip it
            uint64_t kvSize = readSizeEncoding(file);
            uint64_t expirySize = readSizeEncoding(file);
            std::cout << "Hash table sizes: " << kvSize << " keys, " << expirySize << " expiry entries" << std::endl;
        } else if (byte == RDB_EXPIRETIME || byte == RDB_EXPIRETIME_MS) {
            // Expiry information
            std::chrono::steady_clock::time_point expiry;
            bool has_expiry = true;

            if (byte == RDB_EXPIRETIME) {
                // Expiry time in seconds
                uint32_t expirySeconds;
                file.read(reinterpret_cast<char*>(&expirySeconds), 4);
                std::cout << "Expiry time in seconds: " << expirySeconds << std::endl;

                // Convert to time_point
                auto now = std::chrono::steady_clock::now();
                auto systemNow = std::chrono::system_clock::now();
                auto expiryTime = std::chrono::system_clock::from_time_t(expirySeconds);

                // Calculate the difference and apply it to steady_clock
                auto diff = expiryTime - systemNow;
                expiry = now + diff;
            } else {
                // Expiry time in milliseconds
                uint64_t expiryMilliseconds;
                file.read(reinterpret_cast<char*>(&expiryMilliseconds), 8);
                std::cout << "Expiry time in milliseconds: " << expiryMilliseconds << std::endl;

                // Convert to time_point
                auto now = std::chrono::steady_clock::now();
                auto systemNow = std::chrono::system_clock::now();
                auto expiryTime = std::chrono::system_clock::time_point(
                    std::chrono::milliseconds(expiryMilliseconds));

                // Calculate the difference and apply it to steady_clock
                auto diff = expiryTime - systemNow;
                expiry = now + diff;
            }

            // Read the value type
            uint8_t valueType;
            file.read(reinterpret_cast<char*>(&valueType), 1);
            std::cout << "Value type: " << static_cast<int>(valueType) << std::endl;

            // For now, we only support string values
            if (valueType != RDB_TYPE_STRING) {
                std::cout << "Unsupported value type: " << static_cast<int>(valueType) << std::endl;
                return false;
            }

            // Read the key
            std::string key = readStringEncoding(file);
            std::cout << "Read key with expiry: " << key << std::endl;

            // Read the value
            std::string value = readStringEncoding(file);
            std::cout << "Read value: " << value << std::endl;

            // Store the key-value pair with expiry
            data_store[key] = RedisValue(value, expiry, has_expiry);
            std::cout << "Stored key-value pair with expiry" << std::endl;
        } else if (byte == RDB_TYPE_STRING) {
            // String value without expiry
            std::cout << "String value without expiry" << std::endl;

            // Read the key
            std::string key = readStringEncoding(file);
            std::cout << "Read key: " << key << std::endl;

            // Read the value
            std::string value = readStringEncoding(file);
            std::cout << "Read value: " << value << std::endl;

            // Store the key-value pair without expiry
            data_store[key] = RedisValue(value);
            std::cout << "Stored key-value pair without expiry" << std::endl;
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
    // Write redis-ver metadata
    file.put(RDB_METADATA);
    writeStringEncoding(file, "redis-ver");
    writeStringEncoding(file, "6.0.16");

    // Write redis-bits metadata
    file.put(RDB_METADATA);
    writeStringEncoding(file, "redis-bits");
    writeStringEncoding(file, "64");
}

void RdbFile::writeDatabase(std::ofstream& file, const std::unordered_map<std::string, RedisValue>& data_store) {
    // Select database 0
    file.put(RDB_SELECTDB);
    writeSizeEncoding(file, 0);

    // Write hash table size information
    file.put(RDB_HASH_TABLE_SIZE);
    writeSizeEncoding(file, data_store.size()); // Key-value hash table size

    // Count keys with expiry
    size_t expiryCount = 0;
    for (const auto& [key, value] : data_store) {
        if (value.has_expiry) {
            expiryCount++;
        }
    }
    writeSizeEncoding(file, expiryCount); // Expires hash table size

    // Write key-value pairs
    for (const auto& [key, value] : data_store) {
        if (value.has_expiry) {
            // Write expiry information in milliseconds
            file.put(RDB_EXPIRETIME_MS);

            // Convert expiry time to milliseconds since epoch
            auto now = std::chrono::steady_clock::now();
            auto systemNow = std::chrono::system_clock::now();
            auto diff = value.expiry - now;
            auto expiryTime = systemNow + diff;
            auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                expiryTime.time_since_epoch()).count();

            // Write expiry time as 8-byte unsigned long in little-endian
            file.write(reinterpret_cast<const char*>(&milliseconds), 8);
        }

        // Write value type
        file.put(RDB_TYPE_STRING);

        // Write key
        writeStringEncoding(file, key);

        // Write value
        writeStringEncoding(file, value.value);
    }
}

void RdbFile::writeEndOfFile(std::ofstream& file) {
    // Write EOF marker
    file.put(RDB_EOF);

    // Write checksum (8 bytes of zeros for simplicity)
    uint64_t checksum = 0;
    file.write(reinterpret_cast<const char*>(&checksum), 8);
}

uint64_t RdbFile::readSizeEncoding(std::ifstream& file) {
    uint8_t byte;
    file.read(reinterpret_cast<char*>(&byte), 1);

    std::cout << "Size encoding byte: 0x" << std::hex << static_cast<int>(byte) << std::dec << std::endl;

    // Check the first two bits
    uint8_t firstTwoBits = (byte & 0xC0) >> 6;
    std::cout << "First two bits: " << static_cast<int>(firstTwoBits) << std::endl;

    if (firstTwoBits == 0) {
        // Size is the remaining 6 bits
        uint64_t size = byte & 0x3F;
        std::cout << "Size (6 bits): " << size << std::endl;
        return size;
    } else if (firstTwoBits == 1) {
        // Size is the next 14 bits
        uint8_t nextByte;
        file.read(reinterpret_cast<char*>(&nextByte), 1);

        uint16_t size = ((byte & 0x3F) << 8) | nextByte;
        std::cout << "Size (14 bits): " << size << std::endl;
        return size;
    } else if (firstTwoBits == 2) {
        // Size is the next 4 bytes
        uint32_t size;
        file.read(reinterpret_cast<char*>(&size), 4);

        // Convert from big-endian to host byte order
        size = ((size & 0xFF000000) >> 24) |
               ((size & 0x00FF0000) >> 8) |
               ((size & 0x0000FF00) << 8) |
               ((size & 0x000000FF) << 24);

        std::cout << "Size (32 bits): " << size << std::endl;
        return size;
    } else {
        // Special string encoding
        if (byte == 0xC0) {
            // 8-bit integer
            uint8_t intValue;
            file.read(reinterpret_cast<char*>(&intValue), 1);
            std::cout << "8-bit integer: " << static_cast<int>(intValue) << std::endl;
            return intValue;
        } else if (byte == 0xC1) {
            // 16-bit integer
            uint16_t intValue;
            file.read(reinterpret_cast<char*>(&intValue), 2);

            // Convert from little-endian to host byte order
            intValue = ((intValue & 0xFF00) >> 8) |
                       ((intValue & 0x00FF) << 8);

            std::cout << "16-bit integer: " << intValue << std::endl;
            return intValue;
        } else if (byte == 0xC2) {
            // 32-bit integer
            uint32_t intValue;
            file.read(reinterpret_cast<char*>(&intValue), 4);

            // Convert from little-endian to host byte order
            intValue = ((intValue & 0xFF000000) >> 24) |
                       ((intValue & 0x00FF0000) >> 8) |
                       ((intValue & 0x0000FF00) << 8) |
                       ((intValue & 0x000000FF) << 24);

            std::cout << "32-bit integer: " << intValue << std::endl;
            return intValue;
        } else {
            // Unsupported encoding
            std::cout << "Unsupported string encoding: " << static_cast<int>(byte) << std::endl;
            return 0;
        }
    }
}

std::string RdbFile::readStringEncoding(std::ifstream& file) {
    uint64_t size = readSizeEncoding(file);

    // Read the string
    std::string str(size, '\0');
    file.read(&str[0], size);

    std::cout << "Read string: " << str << std::endl;
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
    // Write the size
    writeSizeEncoding(file, str.size());

    // Write the string
    file.write(str.c_str(), str.size());
}

uint64_t RdbFile::calculateCRC64(const std::string& data) {
    // Simplified CRC64 calculation (returns 0 for now)
    return 0;
}
