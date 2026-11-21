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

    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (result != 0) {
        std::cerr << "WSAStartup error=" << result << std::endl;
        return 1;
    }

    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (clientSocket == INVALID_SOCKET) {
        WSACleanup();
        return 1;
    }

    DWORD timeoutMs = static_cast<DWORD>(timeout) * 1000;

    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &serverAddress.sin_addr) != 1) {
        std::cerr << "Invalid IPv4 address." << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    return 0;
}
