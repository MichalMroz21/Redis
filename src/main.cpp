#include "redis_server.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    try {
        asio::io_context io_context;

        RedisServer server(io_context);

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];

            if (arg == "--dir" && i + 1 < argc) {
                server.setConfig("dir", argv[++i]);
            } else if (arg == "--dbfilename" && i + 1 < argc) {
                server.setConfig("dbfilename", argv[++i]);
            }
        }

        server.start();
        io_context.run();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
