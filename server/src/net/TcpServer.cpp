#include "net/TcpServer.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>
#include <iostream>

#include "util/Logger.hpp"

namespace orbital::net {

using orbital::util::Logger;

#ifdef _WIN32
using RecvLenType = int;
using SockLenType = int;
#else
using RecvLenType = ssize_t;
using SockLenType = socklen_t;
#endif

TcpServer::TcpServer() = default;

TcpServer::~TcpServer() { stop(); }

bool TcpServer::start(int port) {
    if (running_) {
        return true;
    }
#ifdef _WIN32
    if (!winsockInitialized_) {
        WSADATA wsaData;
        int wsaResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (wsaResult != 0) {
            ORBITAL_LOG(Logger::Level::Error, "WSAStartup failed: ", wsaResult);
            return false;
        }
        winsockInitialized_ = true;
    }
#endif
    running_ = true;
    acceptThread_ = std::thread(&TcpServer::acceptLoop, this, port);
    return true;
}

void TcpServer::stop() {
    if (!running_) {
#ifdef _WIN32
        if (winsockInitialized_) {
            WSACleanup();
            winsockInitialized_ = false;
        }
#endif
        return;
    }
    running_ = false;
    closeSocket(serverSocket_);
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& [id, client] : clients_) {
        client->running = false;
        closeSocket(client->socketFd);
        if (client->thread.joinable()) {
            client->thread.join();
        }
    }
    clients_.clear();
#ifdef _WIN32
    if (winsockInitialized_) {
        WSACleanup();
        winsockInitialized_ = false;
    }
#endif
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
    if (client->socketFd == INVALID_SOCKET_VALUE) {
        return;
    }
    std::string payload = text;
    if (payload.empty() || payload.back() != '\n') {
        payload.push_back('\n');
    }
    ::send(client->socketFd, payload.c_str(), static_cast<int>(payload.size()), 0);
}

void TcpServer::acceptLoop(int port) {
    serverSocket_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (serverSocket_ == INVALID_SOCKET_VALUE) {
        ORBITAL_LOG(Logger::Level::Error, "Failed to create socket");
        return;
    }
    int opt = 1;
    setsockopt(serverSocket_, SOL_SOCKET, SO_REUSEADDR,
               reinterpret_cast<const char*>(&opt), sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(serverSocket_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        ORBITAL_LOG(Logger::Level::Error, "Failed to bind socket");
        closeSocket(serverSocket_);
        return;
    }
    if (listen(serverSocket_, 8) < 0) {
        ORBITAL_LOG(Logger::Level::Error, "Failed to listen on socket");
        closeSocket(serverSocket_);
        return;
    }
    ORBITAL_LOG(Logger::Level::Info, "TCP server listening on port ", port);
    int nextId = 1;
    while (running_) {
        sockaddr_in clientAddr{};
        SockLenType len = static_cast<SockLenType>(sizeof(clientAddr));
        SocketType clientSocket = accept(serverSocket_, reinterpret_cast<sockaddr*>(&clientAddr), &len);
        if (clientSocket == INVALID_SOCKET_VALUE) {
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
        RecvLenType received = recv(client->socketFd, temp, static_cast<int>(sizeof(temp)), 0);
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
    closeSocket(client->socketFd);
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.erase(client->id);
    }
}

void TcpServer::closeSocket(SocketType& socketFd) {
    if (socketFd == INVALID_SOCKET_VALUE) {
        return;
    }
#ifdef _WIN32
    closesocket(socketFd);
#else
    close(socketFd);
#endif
    socketFd = INVALID_SOCKET_VALUE;
}

} // namespace orbital::net
