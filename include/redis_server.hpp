#ifndef REDIS_SERVER_HPP
#define REDIS_SERVER_HPP

#include <iostream>
#include <string>
#include <vector>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

class RedisServer {
public:
    RedisServer(int port = 6379);
    ~RedisServer();

    void run();

private:
    int port_;
    int server_fd_;
    std::vector<int> client_fds_;
    bool running_;

    bool initialize();

    static void setNonBlocking(int sock);

    void acceptNewConnection();
    void handleClientData(int client_fd);

    bool isPingCommand(const std::string& buffer);
    void sendPong(int client_fd);

    void printReceivedData(const char* buffer, ssize_t bytes_received);

    void cleanup();
};

#endif
