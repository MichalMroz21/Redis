#include "redis_server.hpp"
#include <iostream>

int main() {
  try {
    asio::io_context io_context;

    RedisServer server(io_context);
    server.start();

    io_context.run();
  }
  catch (std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }

  return 0;
}
