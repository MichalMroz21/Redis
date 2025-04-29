#ifndef RESP_PARSER_HPP
#define RESP_PARSER_HPP

#include <string>
#include <vector>
#include <optional>
#include <stdexcept>

// RESP data types
enum class RespType {
    SimpleString,
    Error,
    Integer,
    BulkString,
    Array,
    Unknown
};

// Class to represent a RESP value
class RespValue {
public:
    RespValue() : type_(RespType::Unknown) {}

    // Constructors for different types
    static RespValue createSimpleString(const std::string& value) {
        RespValue resp;
        resp.type_ = RespType::SimpleString;
        resp.string_value_ = value;
        return resp;
    }

    static RespValue createError(const std::string& value) {
        RespValue resp;
        resp.type_ = RespType::Error;
        resp.string_value_ = value;
        return resp;
    }

    static RespValue createInteger(int64_t value) {
        RespValue resp;
        resp.type_ = RespType::Integer;
        resp.int_value_ = value;
        return resp;
    }

    static RespValue createBulkString(const std::string& value) {
        RespValue resp;
        resp.type_ = RespType::BulkString;
        resp.string_value_ = value;
        return resp;
    }

    static RespValue createArray(const std::vector<RespValue>& values) {
        RespValue resp;
        resp.type_ = RespType::Array;
        resp.array_values_ = values;
        return resp;
    }

    // Getters
    RespType getType() const { return type_; }
    const std::string& getStringValue() const { return string_value_; }
    int64_t getIntValue() const { return int_value_; }
    const std::vector<RespValue>& getArrayValues() const { return array_values_; }

    // Encode to RESP format
    std::string encode() const {
        switch (type_) {
            case RespType::SimpleString:
                return "+" + string_value_ + "\r\n";
            case RespType::Error:
                return "-" + string_value_ + "\r\n";
            case RespType::Integer:
                return ":" + std::to_string(int_value_) + "\r\n";
            case RespType::BulkString:
                return "$" + std::to_string(string_value_.length()) + "\r\n" + string_value_ + "\r\n";
            case RespType::Array: {
                std::string result = "*" + std::to_string(array_values_.size()) + "\r\n";
                for (const auto& value : array_values_) {
                    result += value.encode();
                }
                return result;
            }
            default:
                return "";
        }
    }

private:
    RespType type_;
    std::string string_value_;
    int64_t int_value_ = 0;
    std::vector<RespValue> array_values_;
};

// Class to parse RESP protocol
class RespParser {
public:
    // Decode a RESP message into a vector of strings (command and arguments)
    static std::vector<std::string> decode(const std::string& data);

    // Encode a simple string response
    static std::string encodeSimpleString(const std::string& str);

    // Encode an error response
    static std::string encodeError(const std::string& err);

    // Encode a bulk string response
    static std::string encodeBulkString(const std::string& str);

    // Encode an array of strings
    static std::string encodeArray(const std::vector<std::string>& values);

    // Parse a buffer containing RESP data
    static std::optional<RespValue> parse(const std::string& data, size_t& pos) {
        if (pos >= data.length()) {
            return std::nullopt;
        }

        char type = data[pos++];

        switch (type) {
            case '+': // Simple String
                return parseSimpleString(data, pos);
            case '-': // Error
                return parseError(data, pos);
            case ':': // Integer
                return parseInteger(data, pos);
            case '$': // Bulk String
                return parseBulkString(data, pos);
            case '*': // Array
                return parseArray(data, pos);
            default:
                throw std::runtime_error("Unknown RESP type");
        }
    }

    // Parse a complete command from the buffer
    static std::optional<std::vector<std::string>> parseCommand(const std::string& data) {
        size_t pos = 0;
        auto result = parse(data, pos);

        if (!result || result->getType() != RespType::Array) {
            return std::nullopt;
        }

        std::vector<std::string> command;
        for (const auto& value : result->getArrayValues()) {
            if (value.getType() != RespType::BulkString) {
                return std::nullopt;
            }
            command.push_back(value.getStringValue());
        }

        return command;
    }

private:
    static std::optional<RespValue> parseSimpleString(const std::string& data, size_t& pos) {
        std::string value;
        while (pos < data.length() && data[pos] != '\r') {
            value += data[pos++];
        }

        if (pos + 1 >= data.length() || data[pos] != '\r' || data[pos + 1] != '\n') {
            return std::nullopt;
        }

        pos += 2; // Skip \r\n
        return RespValue::createSimpleString(value);
    }

    static std::optional<RespValue> parseError(const std::string& data, size_t& pos) {
        std::string value;
        while (pos < data.length() && data[pos] != '\r') {
            value += data[pos++];
        }

        if (pos + 1 >= data.length() || data[pos] != '\r' || data[pos + 1] != '\n') {
            return std::nullopt;
        }

        pos += 2; // Skip \r\n
        return RespValue::createError(value);
    }

    static std::optional<RespValue> parseInteger(const std::string& data, size_t& pos) {
        std::string value;
        while (pos < data.length() && data[pos] != '\r') {
            value += data[pos++];
        }

        if (pos + 1 >= data.length() || data[pos] != '\r' || data[pos + 1] != '\n') {
            return std::nullopt;
        }

        pos += 2; // Skip \r\n
        try {
            return RespValue::createInteger(std::stoll(value));
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    static std::optional<RespValue> parseBulkString(const std::string& data, size_t& pos) {
        std::string length_str;
        while (pos < data.length() && data[pos] != '\r') {
            length_str += data[pos++];
        }

        if (pos + 1 >= data.length() || data[pos] != '\r' || data[pos + 1] != '\n') {
            return std::nullopt;
        }

        pos += 2; // Skip \r\n

        int length;
        try {
            length = std::stoi(length_str);
        } catch (const std::exception&) {
            return std::nullopt;
        }

        if (length == -1) {
            // Null bulk string
            return RespValue::createBulkString("");
        }

        if (pos + length + 2 > data.length()) {
            return std::nullopt;
        }

        std::string value = data.substr(pos, length);
        pos += length + 2; // Skip string and \r\n

        return RespValue::createBulkString(value);
    }

    static std::optional<RespValue> parseArray(const std::string& data, size_t& pos) {
        std::string length_str;
        while (pos < data.length() && data[pos] != '\r') {
            length_str += data[pos++];
        }

        if (pos + 1 >= data.length() || data[pos] != '\r' || data[pos + 1] != '\n') {
            return std::nullopt;
        }

        pos += 2; // Skip \r\n

        int length;
        try {
            length = std::stoi(length_str);
        } catch (const std::exception&) {
            return std::nullopt;
        }

        std::vector<RespValue> values;
        for (int i = 0; i < length; i++) {
            auto value = parse(data, pos);
            if (!value) {
                return std::nullopt;
            }
            values.push_back(*value);
        }

        return RespValue::createArray(values);
    }
};

#endif // RESP_PARSER_HPP
