#include "net/TcpServer.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

#include "util/Logger.hpp"

namespace orbital::net {

using orbital::util::Logger;

TcpServer::TcpServer() = default;

TcpServer::~TcpServer() { stop(); }

bool TcpServer::start(int port) {
    if (running_) {
        return true;
    }
    running_ = true;
    acceptThread_ = std::thread(&TcpServer::acceptLoop, this, port);
    return true;
}

void TcpServer::stop() {
    if (!running_) {
        return;
    }
    running_ = false;
    if (serverSocket_ >= 0) {
        close(serverSocket_);
        serverSocket_ = -1;
    }
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& [id, client] : clients_) {
        client->running = false;
        if (client->socketFd >= 0) {
            close(client->socketFd);
        }
        if (client->thread.joinable()) {
            client->thread.join();
        }
    }
    clients_.clear();
}

void TcpServer::pollMessages(std::vector<NetworkMessage>& outMessages) {
    std::lock_guard<std::mutex> lock(messageMutex_);
    outMessages.insert(outMessages.end(), incoming_.begin(), incoming_.end());
    incoming_.clear();
}

void TcpServer::sendMessage(int connectionId, const std::string& text) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    auto it = clients_.find(connectionId);
    if (it == clients_.end()) {
        return;
    }
    auto client = it->second;
    std::string payload = text;
    if (payload.empty() || payload.back() != '\n') {
        payload.push_back('\n');
    }
    ::send(client->socketFd, payload.c_str(), payload.size(), 0);
}

void TcpServer::acceptLoop(int port) {
    serverSocket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ < 0) {
        ORBITAL_LOG(Logger::Level::Error, "Failed to create socket");
        return;
    }
    int opt = 1;
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(serverSocket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ORBITAL_LOG(Logger::Level::Error, "Failed to bind socket");
        return;
    }
    if (listen(serverSocket_, 8) < 0) {
        ORBITAL_LOG(Logger::Level::Error, "Failed to listen on socket");
        return;
    }
    ORBITAL_LOG(Logger::Level::Info, "TCP server listening on port ", port);
    int nextId = 1;
    while (running_) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientSocket = accept(serverSocket_, reinterpret_cast<sockaddr*>(&clientAddr), &len);
        if (clientSocket < 0) {
            if (running_) {
                ORBITAL_LOG(Logger::Level::Warning, "accept failed");
            }
            continue;
        }
        auto client = std::make_shared<ClientConnection>();
        client->id = nextId++;
        client->socketFd = clientSocket;
        client->running = true;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clients_[client->id] = client;
        }
        client->thread = std::thread(&TcpServer::clientLoop, this, client);
    }
}

void TcpServer::clientLoop(std::shared_ptr<ClientConnection> client) {
    ORBITAL_LOG(Logger::Level::Info, "Client connected: ", client->id);
    std::string buffer;
    buffer.reserve(4096);
    char temp[512];
    while (client->running) {
        ssize_t received = recv(client->socketFd, temp, sizeof(temp), 0);
        if (received <= 0) {
            break;
        }
        buffer.append(temp, temp + received);
        size_t pos = 0;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            std::lock_guard<std::mutex> lock(messageMutex_);
            incoming_.push_back({client->id, line});
        }
    }
    ORBITAL_LOG(Logger::Level::Info, "Client disconnected: ", client->id);
    client->running = false;
    close(client->socketFd);
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.erase(client->id);
    }
}

} // namespace orbital::net
