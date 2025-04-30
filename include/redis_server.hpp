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

class RedisSession;

class RedisServer {
public:
    RedisServer(asio::io_context& io_context, int port = 6379);
    void start();
    void removeSession(std::shared_ptr<RedisSession> session);
    std::unordered_map<std::string, std::string>& getDataStore() { return data_store_; }

private:
    void acceptConnection();

    asio::io_context& io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::unordered_set<std::shared_ptr<RedisSession>> sessions_;
    std::unordered_map<std::string, std::string> data_store_;
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
