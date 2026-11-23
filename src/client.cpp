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

const uint32_t MAX_MESSAGE_SIZE = 65536;
std::mutex logMutex;

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
        if (result <= 0) return false;
        received += result;
    }

    return true;
}

bool sendMessage(SOCKET s, const std::string& message)
{
    if (message.empty() || message.size() > MAX_MESSAGE_SIZE) return false;
    uint32_t length = htonl(static_cast<uint32_t>(message.size()));

    if (!sendAll(s, reinterpret_cast<char*>(&length), 4)) return false;
    return sendAll(s, message.data(),static_cast<int>(message.size()));
}

bool receiveMessage(SOCKET s, std::string& message)
{
    uint32_t networkLength = 0;
    if (!receiveAll(s, reinterpret_cast<char*>(&networkLength), 4)) return false;

    uint32_t length = ntohl(networkLength);
    if (length == 0 || length > MAX_MESSAGE_SIZE) return false;

    message.resize(length);
    return receiveAll(s, &message[0], static_cast<int>(length));
}

bool parseResponse(const std::string& message, std::string& requestId, std::string& status, std::string& payload)
{
    size_t p1 = message.find('|');
    if (p1 == std::string::npos) return false;
    size_t p2 = message.find('|', p1 + 1);
    if (p2 == std::string::npos) return false;
    size_t p3 = message.find('|', p2 + 1);
    if (p3 == std::string::npos) return false;
    size_t p4 = message.find('|', p3 + 1);
    if (p4 == std::string::npos) return false;

    std::string version = message.substr(0, p1);
    std::string type = message.substr(p1 + 1, p2 - p1 - 1);

    requestId = message.substr(p2 + 1, p3 - p2 - 1);
    status = message.substr(p3 + 1, p4 - p3 - 1);
    payload = message.substr(p4 + 1);

    return version == "1" && type == "RESP" && !requestId.empty() && (status == "OK" || status == "ERR");
}

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

    std::cout << "\nConnecting to server..." << std::endl;
    auto connectStart = std::chrono::steady_clock::now();

    if (connect(clientSocket, reinterpret_cast<sockaddr*>(&serverAddress), sizeof(serverAddress)) == SOCKET_ERROR) {
        std::cerr << socketError("connect") << std::endl;
        log("CONNECTION_FAILED host=" + host + " port=" + std::to_string(port));

        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    auto connectEnd = std::chrono::steady_clock::now();

    double connectTime = std::chrono::duration<double, std::milli>(connectEnd - connectStart).count();
    log("CONNECTED id=" + std::to_string(clientId) + " host=" + host + " port=" + std::to_string(port));

    std::cout << "Connected successfully!" << std::endl;
    std::cout << "Client ID: " << clientId << std::endl;
    std::cout << "Connection time: " << connectTime << " ms\n";
    std::cout << "\nStarting test: " << count << " commands.\n" << std::endl;

    std::vector<std::string> commands = {
        "HELP",
        "TIME",
        "UPTIME",
        "ECHO Test message",
        "STATS"
    };

    int sentCount = 0;
    int receivedCount = 0;
    int successCount = 0;
    int errorCount = 0;
    double totalTime = 0;

    auto testStart = std::chrono::steady_clock::now();

    for (int i = 1; i <= count; i++) {
        std::string command = commands[(i - 1) % commands.size()];
        std::string request = "1|REQ|" + std::to_string(i) + "|" + std::to_string(clientId) + "|" + command;

        auto requestStart = std::chrono::steady_clock::now();

        if (!sendMessage(clientSocket, request)) {
            log("SEND_ERROR requestId=" + std::to_string(i) + " " + socketError("send"));
            errorCount++;
            break;
        }

        sentCount++;
        log("SEND requestId=" + std::to_string(i) + " command=" + command);
        std::string response;

        if (!receiveMessage(clientSocket, response)) {
            int error = WSAGetLastError();

            if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
                log("TIMEOUT requestId=" + std::to_string(i));
            }
            else {
                log("RECV_ERROR requestId=" + std::to_string(i) + " " + socketError("recv"));
            }

            errorCount++;
            break;
        }

        auto requestEnd = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>( requestEnd - requestStart).count();
        totalTime += elapsed;
        receivedCount++;

        std::string responseId, status, payload;
        if (!parseResponse(response, responseId, status, payload) || responseId != std::to_string(i)) {
            log("PROTOCOL_ERROR requestId=" + std::to_string(i));
            errorCount++;
            continue;
        }

        if (status == "OK") successCount++;
        else errorCount++;

        std::cout << "[" << i << "/" << count << "] "
             << command << " => " << payload
             << " (" << std::fixed << std::setprecision(2)
             << elapsed << " ms)" << std::endl;

        log("RECV requestId=" + responseId + " status=" + status + " time=" + std::to_string(elapsed) + "ms");
    }

    std::string quitRequest = "1|REQ|999999|" + std::to_string(clientId) + "|QUIT";
    if (sendMessage(clientSocket, quitRequest)) {
        std::string response;

        if (receiveMessage(clientSocket, response)) {
            std::string id, status, payload;

            if (parseResponse(response, id, status, payload)) {
                std::cout << "\nServer: " << payload << std::endl;
                log("QUIT_CONFIRMED id=" + std::to_string(clientId));
            }
        }
    }

    auto testEnd = std::chrono::steady_clock::now();
    double totalDuration = std::chrono::duration<double, std::milli>( testEnd - testStart).count();
    double averageTime = receivedCount > 0 ? totalTime / receivedCount : 0;

    std::cout << "\n========== TEST RESULTS ==========\n";

    std::cout << "Client ID: " << clientId << std::endl << "Sent requests: " << sentCount << std::endl
              << "Received: " << receivedCount << std::endl << "Successful: " << successCount << std::endl << "Errors: " << errorCount << std::endl;

    std::cout << "Total time: "<< totalDuration << " ms" << std::endl << "Average latency: " << averageTime << " ms" << std::endl;

    if (totalDuration > 0) {
        std::cout << "Throughput: " << receivedCount * 1000.0 / totalDuration << " requests/sec" << std::endl;
    }

    std::cout << "==================================\n";

    log("TEST_FINISHED id=" + std::to_string(clientId) +
             " sent=" + std::to_string(sentCount) +
             " received=" + std::to_string(receivedCount) +
             " successful=" + std::to_string(successCount) +
             " errors=" + std::to_string(errorCount));

    shutdown(clientSocket, SD_BOTH);
    closesocket(clientSocket);

    WSACleanup();

    std::cout << "\nClient stopped." << std::endl;
    return 0;
}
