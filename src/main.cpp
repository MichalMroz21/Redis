#include "redis_server.hpp"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    try {
        asio::io_context io_context;
        
        int port = 6379;

        std::string dir = "database";
        std::string dbfilename = "save.rdb";

        // Parse command line arguments
        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];

            if (arg == "--port" && i + 1 < argc) {
                try {
                    port = std::stoi(argv[++i]);
                } catch (const std::exception& e) {
                    std::cerr << "Invalid port number: " << argv[i] << std::endl;
                    return 1;
                }
            } else if (arg == "--dir" && i + 1 < argc) {
                dir = argv[++i];
            } else if (arg == "--dbfilename" && i + 1 < argc) {
                dbfilename = argv[++i];
            }
        }

        std::cout << "Starting Redis server on port " << port << std::endl;

        RedisServer server(io_context, port);

        server.setConfig("dir", dir);
        server.setConfig("dbfilename", dbfilename);

        server.start();
        io_context.run();
    }
    catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
