#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <csignal>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <vector>
#include <thread>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <fstream>
#include <algorithm>

std::atomic<bool> running(true);
std::atomic<unsigned long long> requestCount(0);

const uint32_t MAX_MESSAGE_SIZE = 65536;
std::mutex logMutex;
auto startTime = std::chrono::steady_clock::now();

void log(const std::string& event)
{
    std::lock_guard<std::mutex> lock(logMutex);

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    tm localTime{};
    localtime_s(&localTime, &time);

    std::ofstream file("server.log", std::ios::app);

    std::cout << "[" << std::put_time(&localTime, "%H:%M:%S")
         << "." << std::setfill('0') << std::setw(3) << ms
         << "] SERVER " << event << std::endl;

    if (file.is_open()) {
        file << "[" << std::put_time(&localTime, "%H:%M:%S")
             << "." << std::setfill('0') << std::setw(3) << ms
             << "] SERVER " << event << std::endl;
    }
}

std::string socketError(const std::string& functionName)
{
    int code = WSAGetLastError();
    char* message = nullptr;

    FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        (LPSTR)&message,
        0,
        nullptr
    );

    std::string result = functionName + " error=" + std::to_string(code);

    if (message) {
        result += " description=" + std::string(message);
        LocalFree(message);
    }
    return result;
}

void onSignal(int)
{
    running = false;
}

bool sendAll(SOCKET s, const char* data, int size)
{
    int sent = 0;

    while (sent < size) {
        int result = send(s, data + sent, size - sent, 0);
        if (result == SOCKET_ERROR || result == 0) return false;
        sent += result;
    }
    return true;
}

bool receiveAll(SOCKET s, char* data, int size)
{
    int received = 0;

    while (received < size) {
        int result = recv(s, data + received, size - received, 0);
        if (result == 0) return false;
        if (result == SOCKET_ERROR) return false;
        received += result;
    }
    return true;
}

bool sendMessage(SOCKET s, const std::string& message)
{
    if (message.empty() || message.size() > MAX_MESSAGE_SIZE) return false;
    uint32_t length = htonl(static_cast<uint32_t>(message.size()));

    if (!sendAll(s, reinterpret_cast<char*>(&length), 4)) return false;
    return sendAll(s, message.data(), static_cast<int>(message.size()));
}

bool receiveMessage(SOCKET s, std::string& message)
{
    uint32_t networkLength = 0;

    if (!receiveAll(s, reinterpret_cast<char*>(&networkLength), 4)) return false;

    uint32_t length = ntohl(networkLength);

    if (length == 0 || length > MAX_MESSAGE_SIZE) {
        log("PROTOCOL_ERROR invalid_length=" + std::to_string(length));
        return false;
    }

    message.resize(length);
    return receiveAll(s, &message[0], static_cast<int>(length));
}

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
    }

    if (port < 20000 || port > 29999 || timeout < 1 || timeout > 3600) {
        std::cerr << "Port must be 20000-29999; " << "timeout must be 1-3600 seconds." << std::endl;
        return 1;
    }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        std::cerr << "WSAStartup error=" << result << std::endl;
        return 1;
    }

    SOCKET serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (serverSocket == INVALID_SOCKET) {
        //ДОБАВИТЬ СЮДА ЛОГ ОБ ОШИБКЕ
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &serverAddress.sin_addr) != 1) {
        std::cerr << "Invalid IPv4 address" << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (bind(serverSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
        log(socketError("bind"));
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    if (listen(serverSocket, SOMAXCONN) == SOCKET_ERROR) {
        log(socketError("listen"));
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    log("START host=" + host + " port=" + std::to_string(port) + " timeout=" + std::to_string(timeout));

    std::cout << "\nSERVER STARTED\n" << "Host: " << host << std::endl << "Port: " << port << std::endl
              << "Waiting for clients..." << std::endl << "Press Ctrl+C to stop.\n" << std::endl;

    std::vector<std::thread> workers;

    while (running)
    {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);

        timeval tv{};
        tv.tv_sec = 0;
        tv.tv_usec = 500000;

        int ready = select(0, &readSet, nullptr, nullptr, &tv);

        if (ready == SOCKET_ERROR) {
            if (running) log(socketError("select"));
            break;
        }

        if (ready == 0) continue;

        sockaddr_in clientAddress{};
        int addressLength = sizeof(clientAddress);

        SOCKET clientSocket = accept(
            serverSocket,
            reinterpret_cast<sockaddr*>(&clientAddress),
            &addressLength
        );

        if (clientSocket == INVALID_SOCKET)
        {
            if (running)
                log(socketError("accept"));
            continue;
        }

        workers.emplace_back(handleClient, clientSocket, clientAddress, timeout);
    }

    log("SHUTDOWN_START");
    closesocket(serverSocket);

    for (auto& worker : workers) {
        if (worker.joinable()) worker.join();
    }

    log("STOP totalRequests=" + std::to_string(requestCount.load()));
    WSACleanup();

    std::cout << "\nServer stopped." << std::endl;
    return 0;
} 