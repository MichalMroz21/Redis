#include "redis_server.hpp"
#include "resp_parser.hpp"
#include <iostream>
#include <memory>
#include <algorithm>
#include <cctype>

// RedisServer implementation
RedisServer::RedisServer(asio::io_context& io_context, int port)
    : io_context_(io_context),
      acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)) {
    std::cout << "Redis server initialized on port " << port << std::endl;
}

void RedisServer::start() {
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

// RedisSession implementation
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

        // Convert command to lowercase for case-insensitive comparison
        std::string cmd = command[0];
        std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        if (cmd == "ping") {
            if (command.size() > 1) {
                response = RespParser::encodeBulkString(command[1]);
            } else {
                response = RespParser::encodeSimpleString("PONG");
            }
        } else if (cmd == "echo") {
            if (command.size() < 2) {
                response = RespParser::encodeError("ERR wrong number of arguments for 'echo' command");
            } else {
                response = RespParser::encodeBulkString(command[1]);
            }
        } else {
            response = RespParser::encodeError("ERR unknown command '" + command[0] + "'");
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
