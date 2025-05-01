#include "redis_server.hpp"
#include "resp_parser.hpp"
#include "rdb_file.hpp"
#include <iostream>
#include <memory>
#include <algorithm>
#include <cctype>
#include <sstream>
#include <filesystem>

std::string toLower(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                  [](unsigned char c) { return std::tolower(c); });
    return result;
}

RedisServer::RedisServer(asio::io_context& io_context, int port)
    : io_context_(io_context),
      acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {

    //Default save directory and file name
    config_["dir"] = "databases";
    config_["dbfilename"] = "save.rdb";

    std::cout << "Redis server initialized on port " << port << std::endl;
}

void RedisServer::start() {
    printConfig();

    std::cout << "Attempting to load RDB file..." << std::endl;
    bool loaded = loadRdbFile();
    if (loaded) {
        std::cout << "Successfully loaded RDB file" << std::endl;
        std::cout << "Data store now contains " << data_store_.size() << " keys" << std::endl;
    } else {
        std::cout << "Failed to load RDB file or file does not exist" << std::endl;
    }

    acceptConnection();
    std::cout << "Waiting for clients to connect...\n";
    std::cout << "Logs from your program will appear here!\n";
}

void RedisServer::acceptConnection() {
    acceptor_.async_accept(
        [this](std::error_code ec, asio::ip::tcp::socket socket) {
            if (!ec) {
                std::cout << "New client connected: "
                          << socket.remote_endpoint() << std::endl;

                auto session = std::make_shared<RedisSession>(std::move(socket), *this);
                sessions_.insert(session);
                session->start();
            }

            acceptConnection();
        });
}

void RedisServer::removeSession(std::shared_ptr<RedisSession> session) {
    sessions_.erase(session);
}

bool RedisServer::setValue(const std::string& key, const std::string& value) {
    data_store_[key] = RedisValue(value);
    return true;
}

bool RedisServer::setValue(const std::string& key, const std::string& value, std::chrono::milliseconds ttl) {
    data_store_[key] = RedisValue(value, ttl);
    return true;
}

std::optional<std::string> RedisServer::getValue(const std::string& key) {
    auto it = data_store_.find(key);

    if (it == data_store_.end()) {
        return std::nullopt;
    }

    if (it->second.is_expired()) {
        data_store_.erase(it);
        return std::nullopt;
    }

    return it->second.value;
}

std::vector<std::string> RedisServer::getKeys(const std::string& pattern) {
    std::vector<std::string> keys;

    if (pattern == "*") {
        for (const auto& [key, value] : data_store_) {
            if (!value.is_expired()) {
                keys.push_back(key);
            }
        }
    }

    return keys;
}

void RedisServer::setConfig(const std::string& key, const std::string& value) {
    config_[key] = value;
}

std::string RedisServer::getConfig(const std::string& key) const {
    auto it = config_.find(key);
    if (it != config_.end()) {
        return it->second;
    }
    return "";
}

bool RedisServer::hasConfig(const std::string& key) const {
    return config_.find(key) != config_.end();
}

bool RedisServer::loadRdbFile() {
    return RdbFile::loadFromFile(config_["dir"], config_["dbfilename"], data_store_);
}

bool RedisServer::saveRdbFile() {
    return RdbFile::saveToFile(config_["dir"], config_["dbfilename"], data_store_);
}

void RedisServer::printConfig() const {
    std::cout << "Configuration:\n";
    for (const auto& pair : config_) {
        std::cout << "  " << pair.first << ": " << pair.second << "\n";
    }

    std::filesystem::path dirPath = std::filesystem::path(config_.at("dir"));
    std::filesystem::path filePath = dirPath / config_.at("dbfilename");

    std::cout << "  RDB file absolute path: " << std::filesystem::absolute(filePath) << std::endl;
}

RedisSession::RedisSession(asio::ip::tcp::socket socket, RedisServer& server)
    : socket_(std::move(socket)), server_(server), bytes_received_(0) {
}

void RedisSession::start() {
    readData();
}

void RedisSession::readData() {
    auto self(shared_from_this());

    socket_.async_read_some(
        asio::buffer(buffer_),
        [this, self](std::error_code ec, std::size_t bytes_transferred) {
            if (!ec) {
                bytes_received_ = bytes_transferred;
                data_buffer_.append(buffer_.data(), bytes_transferred);

                printReceivedData();
                processData();

                readData();
            } else if (ec != asio::error::operation_aborted) {
                std::cout << "Client disconnected: " << ec.message() << std::endl;
                server_.removeSession(self);
            }
        });
}

void RedisSession::processData() {
    std::vector<std::string> command = RespParser::decode(data_buffer_);

    if (!command.empty()) {
        std::string response;

        std::vector<std::string> originalCommand = command;

        for (auto& str : command) {
            std::transform(str.begin(), str.end(), str.begin(),
                          [](unsigned char c) { return std::tolower(c); });
        }

        if (command[0] == "ping") {
            if (originalCommand.size() > 1) {
                response = RespParser::encodeBulkString(originalCommand[1]);
            } else {
                response = RespParser::encodeSimpleString("PONG");
            }
        } else if (command[0] == "echo") {
            if (originalCommand.size() < 2) {
                response = RespParser::encodeError("ERR wrong number of arguments for 'echo' command");
            } else {
                response = RespParser::encodeBulkString(originalCommand[1]);
            }
        } else if (command[0] == "set") {
            if (originalCommand.size() < 3) {
                response = RespParser::encodeError("ERR wrong number of arguments for 'set' command");
            } else {
                std::string key = originalCommand[1];
                std::string value = originalCommand[2];

                bool has_expiry = false;
                std::chrono::milliseconds ttl(0);

                for (size_t i = 3; i < command.size() - 1; i++) {
                    if (command[i] == "px" && i + 1 < command.size()) {
                        try {
                            int64_t ms = std::stoll(originalCommand[i + 1]);
                            ttl = std::chrono::milliseconds(ms);
                            has_expiry = true;
                            i++;
                        } catch (const std::exception& e) {
                            response = RespParser::encodeError("ERR value is not an integer or out of range");
                            sendResponse(response);
                            data_buffer_.clear();
                            return;
                        }
                    }
                }

                if (has_expiry) {
                    server_.setValue(key, value, ttl);
                } else {
                    server_.setValue(key, value);
                }

                response = RespParser::encodeSimpleString("OK");
            }
        } else if (command[0] == "get") {
            if (originalCommand.size() < 2) {
                response = RespParser::encodeError("ERR wrong number of arguments for 'get' command");
            } else {
                std::string key = originalCommand[1];

                auto value_opt = server_.getValue(key);

                if (value_opt) {
                    response = RespParser::encodeBulkString(*value_opt);
                } else {
                    response = RespParser::encodeNullBulkString();
                }
            }
        } else if (command[0] == "keys") {
            if (originalCommand.size() < 2) {
                response = RespParser::encodeError("ERR wrong number of arguments for 'keys' command");
            } else {
                std::string pattern = originalCommand[1];

                std::vector<std::string> keys = server_.getKeys(pattern);

                response = RespParser::encodeArray(keys);
            }
        } else if (command[0] == "config" && command.size() >= 2) {
            if (command[1] == "get" && command.size() >= 3) {
                std::string param = command[2];

                if (server_.hasConfig(param)) {
                    std::vector<std::string> result = {param, server_.getConfig(param)};
                    response = RespParser::encodeArray(result);
                } else {
                    std::vector<std::string> result;
                    response = RespParser::encodeArray(result);
                }
            } else if (command[1] == "path") {
                std::filesystem::path dirPath = std::filesystem::path(server_.getConfig("dir"));
                std::filesystem::path filePath = dirPath / server_.getConfig("dbfilename");
                std::filesystem::path absolutePath = std::filesystem::absolute(filePath);

                std::vector<std::string> result = {"path", absolutePath.string()};
                response = RespParser::encodeArray(result);
            } else {
                response = RespParser::encodeError("ERR syntax error");
            }
        } else if (command[0] == "save") {
            if (server_.saveRdbFile()) {
                response = RespParser::encodeSimpleString("OK");
            } else {
                response = RespParser::encodeError("ERR failed to save RDB file");
            }
        } else {
            response = RespParser::encodeError("ERR unknown command '" + originalCommand[0] + "'");
        }

        sendResponse(response);
        data_buffer_.clear();
    }
}

void RedisSession::sendResponse(const std::string& response) {
    auto self(shared_from_this());

    asio::async_write(
        socket_,
        asio::buffer(response),
        [this, self](std::error_code ec, std::size_t /*bytes_transferred*/) {
            if (ec && ec != asio::error::operation_aborted) {
                std::cout << "Error sending response: " << ec.message() << std::endl;
                server_.removeSession(self);
            }
        });
}

void RedisSession::printReceivedData() {
    std::cout << "Received: ";

    for (size_t i = 0; i < bytes_received_; i++) {
        if (buffer_[i] == '\r') {
            std::cout << "\\r";
        } else if (buffer_[i] == '\n') {
            std::cout << "\\n";
        } else {
            std::cout << buffer_[i];
        }
    }
    std::cout << std::endl;
}
