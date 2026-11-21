#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>


int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    std::string host = "127.0.0.1";
    int port = 20086;
    int clientId = -1;
    int timeout = 30;
    int count = 100;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (i + 1 >= argc) {
            std::cerr << "Missing value for " << arg << std::endl;
            return 1;
        }

        std::string value = argv[++i];

        try {
            if (arg == "--host")
                host = value;
            else if (arg == "--port")
                port = std::stoi(value);
            else if (arg == "--id")
                clientId = std::stoi(value);
            else if (arg == "--timeout")
                timeout = std::stoi(value);
            else if (arg == "--count")
                count = std::stoi(value);
            else {
                std::cerr << "Unknown parameter: " << arg << std::endl;
                return 1;
            }
        } catch (...) {
            std::cerr << "Invalid parameter value" << std::endl;
            return 1;
        }
    }

    if (clientId < 0 || clientId > 2147483647 || port < 20000 || port > 29999 ||
        timeout < 1 || timeout > 3600 || count < 1 || count > 10000) {
        std::cerr << "Invalid parameters." << std::endl;
        return 1;
    }

    return 0;
}
