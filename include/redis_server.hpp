#ifndef REDIS_SERVER_HPP
#define REDIS_SERVER_HPP

#include <asio.hpp>
#include <iostream>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>

class RedisSession;

// Structure to store a Redis value with its expiry time
struct RedisValue {
    std::string value;
    std::chrono::steady_clock::time_point expiry;
    bool has_expiry;

    // Default constructor
    RedisValue() : value(""), has_expiry(false) {}

    RedisValue(const std::string& val)
        : value(val), has_expiry(false) {}

    RedisValue(const std::string& val, std::chrono::milliseconds ttl)
        : value(val),
          expiry(std::chrono::steady_clock::now() + ttl),
          has_expiry(true) {}

    bool is_expired() const {
        return has_expiry && std::chrono::steady_clock::now() > expiry;
    }
};

class RedisServer {
public:
    RedisServer(asio::io_context& io_context, int port = 6379);
    void start();
    void removeSession(std::shared_ptr<RedisSession> session);

    bool setValue(const std::string& key, const std::string& value);
    bool setValue(const std::string& key, const std::string& value, std::chrono::milliseconds ttl);
    std::optional<std::string> getValue(const std::string& key);

    void setConfig(const std::string& key, const std::string& value);
    std::string getConfig(const std::string& key) const;
    bool hasConfig(const std::string& key) const;

private:
    void acceptConnection();

    asio::io_context& io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::unordered_set<std::shared_ptr<RedisSession>> sessions_;
    std::unordered_map<std::string, RedisValue> data_store_;

    std::unordered_map<std::string, std::string> config_;
};

class RedisSession : public std::enable_shared_from_this<RedisSession> {
public:
    RedisSession(asio::ip::tcp::socket socket, RedisServer& server);
    void start();

private:
    void readData();
    void processData();
    void sendResponse(const std::string& response);
    void printReceivedData();

    asio::ip::tcp::socket socket_;
    RedisServer& server_;
    std::array<char, 1024> buffer_;
    size_t bytes_received_;
    std::string data_buffer_;
};

#endif // REDIS_SERVER_HPP
