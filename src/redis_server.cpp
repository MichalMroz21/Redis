#include <redis_server.hpp>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <algorithm>
#include <errno.h>

RedisServer::RedisServer(int port) : port_(port), server_fd_(-1), running_(false) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
}

RedisServer::~RedisServer() {
    cleanup();
}

bool RedisServer::initialize() {
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd_ < 0) {
        std::cerr << "Failed to create server socket\n";
        return false;
    }

    int reuse = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "setsockopt failed\n";
        close(server_fd_);
        return false;
    }

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port_);

    if (bind(server_fd_, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0) {
        std::cerr << "Failed to bind to port " << port_ << "\n";
        close(server_fd_);
        return false;
    }

    int connection_backlog = 5;
    if (listen(server_fd_, connection_backlog) != 0) {
        std::cerr << "listen failed\n";
        close(server_fd_);
        return false;
    }

    setNonBlocking(server_fd_);

    std::cout << "Server initialized on port " << port_ << "\n";
    return true;
}

void RedisServer::setNonBlocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);

    if (flags == -1) {
        std::cerr << "fcntl F_GETFL failed\n";
        exit(1);
    }
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
        std::cerr << "fcntl F_SETFL O_NONBLOCK failed\n";
        exit(1);
    }
}

void RedisServer::acceptNewConnection() {
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);

    int new_client_fd = accept(server_fd_, (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_len);

    if (new_client_fd < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            std::cerr << "accept failed\n";
        }
        return;
    }

    setNonBlocking(new_client_fd);

    client_fds_.push_back(new_client_fd);

    std::cout << "New client connected, fd: " << new_client_fd << std::endl;
}

void RedisServer::handleClientData(int client_fd) {
    char buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (bytes_received <= 0) {
        if (bytes_received == 0) {
            std::cout << "Client disconnected, fd: " << client_fd << std::endl;
        } else {
            if (errno != EWOULDBLOCK && errno != EAGAIN) {
                std::cerr << "recv error on fd: " << client_fd << std::endl;
            }
        }

        close(client_fd);
        client_fds_.erase(std::remove(client_fds_.begin(), client_fds_.end(), client_fd), client_fds_.end());
        return;
    }

    buffer[bytes_received] = '\0';
    printReceivedData(buffer, bytes_received);

    if (isPingCommand(buffer)) {
        sendPong(client_fd);
    }
}

bool RedisServer::isPingCommand(const std::string& buffer) {
    return buffer.find("PING") != std::string::npos;
}

void RedisServer::sendPong(int client_fd) {
    std::string response = "+PONG\r\n";
    send(client_fd, response.c_str(), response.size(), 0);
    std::cout << "Sent PONG to client fd: " << client_fd << std::endl;
}

void RedisServer::printReceivedData(const char* buffer, ssize_t bytes_received) {
    std::cout << "Received: ";
    std::cout.flush();

    for (size_t i = 0; i < bytes_received; i++) {
        if (buffer[i] == '\r') {
            std::cout << "\\r";
        } else if (buffer[i] == '\n') {
            std::cout << "\\n";
        } else {
            std::cout << buffer[i];
        }
    }
    std::cout << std::endl;
}

void RedisServer::run() {
    if (!initialize()) {
        std::cerr << "Failed to initialize server\n";
        return;
    }

    std::cout << "Waiting for clients to connect...\n";
    std::cout << "Logs from your program will appear here!\n";

    running_ = true;

    while (running_) {
        fd_set read_fds;
        FD_ZERO(&read_fds);

        FD_SET(server_fd_, &read_fds);
        int max_fd = server_fd_;

        for (int client_fd : client_fds_) {
            FD_SET(client_fd, &read_fds);
            max_fd = std::max(max_fd, client_fd);
        }

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

        if (activity < 0) {
            std::cerr << "select error\n";
            break;
        }

        if (FD_ISSET(server_fd_, &read_fds)) {
            acceptNewConnection();
        }

        std::vector<int> client_fds_copy = client_fds_;

        for (int client_fd : client_fds_copy) {
            if (FD_ISSET(client_fd, &read_fds)) {
                handleClientData(client_fd);
            }
        }
    }

    cleanup();
}

void RedisServer::cleanup() {
    for (int client_fd : client_fds_) {
        close(client_fd);
    }
    client_fds_.clear();

    if (server_fd_ != -1) {
        close(server_fd_);
        server_fd_ = -1;
    }
}
