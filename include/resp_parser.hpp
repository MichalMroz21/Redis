#ifndef RESP_PARSER_HPP
#define RESP_PARSER_HPP

#include <string>
#include <vector>
#include <optional>
#include <stdexcept>

class RespParser {
public:
    static std::vector<std::string> decode(const std::string& data);
    static std::string encodeSimpleString(const std::string& str);
    static std::string encodeError(const std::string& err);
    static std::string encodeBulkString(const std::string& str);
    static std::string encodeArray(const std::vector<std::string>& values);
};

#endif // RESP_PARSER_HPP
