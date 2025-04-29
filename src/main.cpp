#include <redis_server.hpp>

int main(int argc, char** argv) {
  RedisServer server;
  server.run();

  return 0;
}