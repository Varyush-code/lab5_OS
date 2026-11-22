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

bool parseRequest(const std::string& message, std::string& version, std::string& type,
    std::string& requestId, std::string& clientId, std::string& command)
{
    size_t pos = 0;
    size_t next;

    std::string* fields[] = {
        &version, &type, &requestId, &clientId
    };

    for (int i = 0; i < 4; i++) {
        next = message.find('|', pos);
        if (next == std::string::npos) return false;
        *fields[i] = message.substr(pos, next - pos);
        pos = next + 1;
    }

    command = message.substr(pos);
    return !version.empty() && !type.empty() && !requestId.empty() && !clientId.empty() && !command.empty();
}

bool validNumber(const std::string& value)
{
    if (value.empty() || value.size() > 10) return false;

    return all_of(value.begin(), value.end(),
        [](unsigned char c) {
            return c >= '0' && c <= '9';
        });
}

std::string currentTime()
{
    auto now = std::chrono::system_clock::now();
    time_t time = std::chrono::system_clock::to_time_t(now);

    tm localTime{};
    localtime_s(&localTime, &time);

    std::stringstream ss;

    ss << std::put_time(&localTime, "%d.%m.%Y %H:%M:%S");

    return ss.str();
}

std::string getUptime()
{
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>( std::chrono::steady_clock::now() - startTime).count();

    long long hours = elapsed / 3600;
    long long minutes = (elapsed % 3600) / 60;
    long long seconds = elapsed % 60;

    return std::to_string(hours) + " h " +
           std::to_string(minutes) + " min " +
           std::to_string(seconds) + " sec";
}


std::string executeCommand(const std::string& command)
{
    if (command == "HELP") {
        return
            "Available commands:\n"
            "HELP - show commands\n"
            "TIME - server time\n"
            "UPTIME - server uptime\n"
            "ECHO text - return text\n"
            "STATS - processed requests\n"
            "QUIT - disconnect";
    }

    if (command == "TIME") return currentTime();
    if (command == "UPTIME") return getUptime();
    if (command == "STATS") return "Processed requests: " + std::to_string(requestCount.load());
    if (command.rfind("ECHO ", 0) == 0) return command.substr(5);
    return "Unknown command: " + command;
}

void handleClient(SOCKET clientSocket, sockaddr_in clientAddress, int timeout)
{
    char ip[INET_ADDRSTRLEN]{};

    inet_ntop(AF_INET, &clientAddress.sin_addr, ip, sizeof(ip));
    int port = ntohs(clientAddress.sin_port);

    std::string address = std::string(ip) + ":" + std::to_string(port);
    DWORD timeoutMs = static_cast<DWORD>(timeout) * 1000;

    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
    setsockopt(clientSocket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));

    std::string clientId = "unknown";
    log("CLIENT_CONNECTED address=" + address);

    while (running) {
        std::string message;

        if (!receiveMessage(clientSocket, message)) {
            log("CLIENT_DISCONNECTED id=" + clientId + " address=" + address + " reason=connection_closed_or_timeout");
            break;
        }

        std::string version, type, requestId, receivedId, command;

        if (!parseRequest(message, version, type, requestId, receivedId, command)) {
            log("PROTOCOL_ERROR address=" + address);
            sendMessage(clientSocket, "1|RESP|0|ERR|Invalid request format");
            break;
        }

        clientId = receivedId;

        if (version != "1" || type != "REQ" || !validNumber(requestId) || !validNumber(clientId)) {
            sendMessage(clientSocket, "1|RESP|" + requestId + "|ERR|Invalid protocol fields");
            log("PROTOCOL_ERROR id=" + clientId);
            break;
        }

        if (command.size() > 60000) {
            sendMessage(clientSocket, "1|RESP|" + requestId + "|ERR|Command is too long");
            break;
        }

        unsigned long long current = ++requestCount;

        log("RECV id=" + clientId + " requestId=" + requestId + " command=" + command);
        std::string response;
        std::string status = "OK";

        if (command == "QUIT") {
            response = "Session terminated";
        }
        else {
            bool known =
                command == "HELP" ||
                command == "TIME" ||
                command == "UPTIME" ||
                command == "STATS" ||
                command.rfind("ECHO ", 0) == 0;

            if (!known) status = "ERR";
            //response = executeCommand(command);
        }

        std::string answer = "1|RESP|" + requestId + "|" + status + "|" + response;

        if (!sendMessage(clientSocket, answer)) {
            log("SEND_ERROR id=" + clientId + " " + socketError("send"));
            break;
        }

        log("SEND id=" + clientId + " requestId=" + requestId +
                 " status=" + status + " processed=" + std::to_string(current));

        if (command == "QUIT") break;
    }

    shutdown(clientSocket, SD_BOTH);
    closesocket(clientSocket);

    log("CLIENT_SESSION_CLOSED id=" + clientId);
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