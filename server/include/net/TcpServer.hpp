#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace orbital::net {

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
        int socketFd{-1};
        std::thread thread;
        std::atomic<bool> running{false};
    };

    int serverSocket_{-1};
    std::atomic<bool> running_{false};
    std::thread acceptThread_;
    std::mutex clientsMutex_;
    std::map<int, std::shared_ptr<ClientConnection>> clients_;

    std::mutex messageMutex_;
    std::vector<NetworkMessage> incoming_;

    void acceptLoop(int port);
    void clientLoop(std::shared_ptr<ClientConnection> client);
};

} // namespace orbital::net
