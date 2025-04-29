#include "resp_parser.hpp"

std::vector<std::string> RespParser::decode(const std::string& data) {
    std::vector<std::string> result;

    // Check if we have enough data
    if (data.empty() || data[0] != '*') {
        return result;
    }

    // Find the first line end
    size_t pos = data.find("\r\n");
    if (pos == std::string::npos) {
        return result;
    }

    // Parse the number of elements
    int count = 0;
    try {
        count = std::stoi(data.substr(1, pos - 1));
    } catch (...) {
        return result;
    }

    // Skip the header
    size_t current_pos = pos + 2;

    // Parse each element
    for (int i = 0; i < count; i++) {
        // Check if we have enough data
        if (current_pos >= data.size() || data[current_pos] != '$') {
            return std::vector<std::string>(); // Not enough data or wrong format
        }

        // Find the length of the bulk string
        size_t len_end = data.find("\r\n", current_pos);
        if (len_end == std::string::npos) {
            return std::vector<std::string>(); // Not enough data
        }

        // Parse the length
        int length = 0;
        try {
            length = std::stoi(data.substr(current_pos + 1, len_end - current_pos - 1));
        } catch (...) {
            return std::vector<std::string>(); // Invalid format
        }

        // Skip the length header
        current_pos = len_end + 2;

        // Check if we have enough data for the string
        if (current_pos + length + 2 > data.size()) {
            return std::vector<std::string>(); // Not enough data
        }

        // Extract the string
        result.push_back(data.substr(current_pos, length));

        // Move to the next element
        current_pos += length + 2;
    }

    return result;
}

std::string RespParser::encodeSimpleString(const std::string& str) {
    return "+" + str + "\r\n";
}

std::string RespParser::encodeError(const std::string& err) {
    return "-" + err + "\r\n";
}

std::string RespParser::encodeBulkString(const std::string& str) {
    return "$" + std::to_string(str.length()) + "\r\n" + str + "\r\n";
}

std::string RespParser::encodeArray(const std::vector<std::string>& values) {
    std::string result = "*" + std::to_string(values.size()) + "\r\n";

    for (const auto& value : values) {
        result += encodeBulkString(value);
    }

    return result;
}
