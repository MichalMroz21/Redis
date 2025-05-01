#ifndef RDB_FILE_HPP
#define RDB_FILE_HPP

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <fstream>
#include <cstdint>

struct RedisValue;

class RdbFile {
public:
    static bool loadFromFile(const std::string& dir, const std::string& filename,
                            std::unordered_map<std::string, RedisValue>& data_store);

    static bool saveToFile(const std::string& dir, const std::string& filename,
                          const std::unordered_map<std::string, RedisValue>& data_store);

private:
    static bool readHeader(std::ifstream& file);
    static void skipMetadata(std::ifstream& file);
    static bool readDatabase(std::ifstream& file, std::unordered_map<std::string, RedisValue>& data_store);

    static void writeHeader(std::ofstream& file);
    static void writeMetadata(std::ofstream& file);
    static void writeDatabase(std::ofstream& file, const std::unordered_map<std::string, RedisValue>& data_store);
    static void writeEndOfFile(std::ofstream& file);

    static uint64_t readSizeEncoding(std::ifstream& file);
    static std::string readStringEncoding(std::ifstream& file);

    static void writeSizeEncoding(std::ofstream& file, uint64_t size);
    static void writeStringEncoding(std::ofstream& file, const std::string& str);

    static uint64_t calculateCRC64(const std::string& data);
};

#endif // RDB_FILE_HPP
