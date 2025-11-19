#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#else
#include <sys/types.h>
#endif

namespace orbital::net {

#ifdef _WIN32
using SocketType = SOCKET;
static constexpr SocketType INVALID_SOCKET_VALUE = INVALID_SOCKET;
#else
using SocketType = int;
static constexpr SocketType INVALID_SOCKET_VALUE = -1;
#endif

struct NetworkMessage {
    int connectionId{-1};
    std::string text;
};

class TcpServer {
public:
    TcpServer();
    ~TcpServer();

    bool start(int port);
    void stop();

    void pollMessages(std::vector<NetworkMessage>& outMessages);
    void sendMessage(int connectionId, const std::string& text);

private:
    struct ClientConnection {
        int id{-1};
        SocketType socketFd{INVALID_SOCKET_VALUE};
        std::thread thread;
        std::atomic<bool> running{false};
    };

    SocketType serverSocket_{INVALID_SOCKET_VALUE};
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    std::mutex clientsMutex_;
    std::map<int, std::shared_ptr<ClientConnection>> clients_;

    std::mutex messageMutex_;
    std::vector<NetworkMessage> incoming_;

#ifdef _WIN32
    bool winsockInitialized_{false};
#endif

    void acceptLoop(int port);
    void clientLoop(std::shared_ptr<ClientConnection> client);
    static void closeSocket(SocketType& socketFd);
};

} // namespace orbital::net
