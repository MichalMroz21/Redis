#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <vector>
#include <algorithm>

void set_nonblocking(int sock) {
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

void print_received(const char *buffer, ssize_t bytes_received) {
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

bool is_ping_command(const char *buffer) {
  std::string buf(buffer);
  return buf.find("PING") != std::string::npos;
}

int main(int argc, char **argv) {
  // Flush after every std::cout / std::cerr
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  int server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
   std::cerr << "Failed to create server socket\n";
   return 1;
  }

  int reuse = 1;
  if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
    std::cerr << "setsockopt failed\n";
    return 1;
  }

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = INADDR_ANY;
  server_addr.sin_port = htons(6379);

  if (bind(server_fd, (struct sockaddr *) &server_addr, sizeof(server_addr)) != 0) {
    std::cerr << "Failed to bind to port 6379\n";
    return 1;
  }

  int connection_backlog = 5;
  if (listen(server_fd, connection_backlog) != 0) {
    std::cerr << "listen failed\n";
    return 1;
  }

  set_nonblocking(server_fd);

  std::cout << "Waiting for clients to connect...\n";
  std::cout << "Logs from your program will appear here!\n";

  std::vector<int> client_fds;

  while (true) {
    fd_set read_fds;
    FD_ZERO(&read_fds);

    FD_SET(server_fd, &read_fds);
    int max_fd = server_fd;

    for (int client_fd : client_fds) {
      FD_SET(client_fd, &read_fds);
      max_fd = std::max(max_fd, client_fd);
    }

    int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);

    if (activity < 0) {
      std::cerr << "select error\n";
      break;
    }

    if (FD_ISSET(server_fd, &read_fds)) {
      struct sockaddr_in client_addr;
      int client_addr_len = sizeof(client_addr);

      int new_client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);

      if (new_client_fd < 0) {
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
          std::cerr << "accept failed\n";
        }
      } else {
        set_nonblocking(new_client_fd);

        client_fds.push_back(new_client_fd);

        std::cout << "New client connected, fd: " << new_client_fd << std::endl;
      }
    }

    for (auto it = client_fds.begin(); it != client_fds.end(); ) {
      int client_fd = *it;

      if (FD_ISSET(client_fd, &read_fds)) {
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
          it = client_fds.erase(it);
        } else {
          buffer[bytes_received] = '\0';
          print_received(buffer, bytes_received);

          if (is_ping_command(buffer)) {
            std::string response = "+PONG\r\n";
            send(client_fd, response.c_str(), response.size(), 0);
            std::cout << "Sent PONG to client fd: " << client_fd << std::endl;
          }

          ++it;
        }
      } else {
        ++it;
      }
    }
  }

  for (int client_fd : client_fds) {
    close(client_fd);
  }

  close(server_fd);

  return 0;
}
