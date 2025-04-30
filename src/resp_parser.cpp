#include "resp_parser.hpp"

std::vector<std::string> RespParser::decode(const std::string& data) {
    std::vector<std::string> result;

    if (data.empty() || data[0] != '*') {
        return result;
    }

    size_t pos = data.find("\r\n");

    if (pos == std::string::npos) {
        return result;
    }

    int count = 0;

    try {
        count = std::stoi(data.substr(1, pos - 1));
    } catch (...) {
        return result;
    }

    size_t current_pos = pos + 2;

    for (int i = 0; i < count; i++) {
        if (current_pos >= data.size() || data[current_pos] != '$') {
            return std::vector<std::string>();
        }

        size_t len_end = data.find("\r\n", current_pos);

        if (len_end == std::string::npos) {
            return std::vector<std::string>();
        }

        int length = 0;

        try {
            length = std::stoi(data.substr(current_pos + 1, len_end - current_pos - 1));
        } catch (...) {
            return std::vector<std::string>();
        }

        current_pos = len_end + 2;

        if (current_pos + length + 2 > data.size()) {
            return std::vector<std::string>();
        }

        result.push_back(data.substr(current_pos, length));

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

std::string RespParser::encodeNullBulkString() {
    return "$-1\r\n";
}
