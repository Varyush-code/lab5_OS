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
    int timeout = 30;

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
                port = stoi(value);
            else if (arg == "--timeout")
                timeout = stoi(value);
            else {
                std::cerr << "Unknown parameter: " << arg << std::endl;
                return 1;
            }
        } catch (...) {
            std::cerr << "Invalid parameter value" << std::endl;
            return 1;
        }

        if (port < 20000 || port > 29999 || timeout < 1 || timeout > 3600) {
            std::cerr << "Port must be 20000-29999; " << "timeout must be 1-3600 seconds." << std::endl;
            return 1;
        }
    }

    return 0;
}
