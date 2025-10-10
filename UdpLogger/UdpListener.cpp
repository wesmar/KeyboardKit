#include "UdpListener.h"
#include "config.h"
#include <iostream>
#include <stdexcept>
#include <algorithm>

#pragma comment(lib, "ws2_32.lib")

UdpListener::UdpListener(const std::string& address, int port)
    : address_(address), port_(port), socket_(INVALID_SOCKET), running_(false) {
    
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        throw std::runtime_error("WSAStartup failed: " + std::to_string(result));
    }
}

UdpListener::~UdpListener() {
    stop();
    cleanup();
    WSACleanup();
}

bool UdpListener::start() {
    if (running_) {
        return true;
    }
    
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == INVALID_SOCKET) {
        std::cerr << "Socket creation failed: " << WSAGetLastError() << std::endl;
        return false;
    }
    
    // Allow socket reuse
    int enable = 1;
    if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&enable, sizeof(enable)) == SOCKET_ERROR) {
        std::cerr << "setsockopt failed: " << WSAGetLastError() << std::endl;
    }
    
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port_);
    inet_pton(AF_INET, address_.c_str(), &serverAddr.sin_addr);
    
    if (bind(socket_, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed: " << WSAGetLastError() << std::endl;
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    
    // Set socket to non-blocking mode for better shutdown handling
    u_long mode = 1; // 1 to enable non-blocking mode
    ioctlsocket(socket_, FIONBIO, &mode);
    
    running_ = true;
    listenerThread_ = std::thread(&UdpListener::run, this);
    
	// Service mode - no console output
    // std::cout << "UDP listener started on " << address_ << ":" << port_ << std::endl;
    return true;
}

void UdpListener::stop() {
    if (!running_) {
        return;
    }
    
    running_ = false;
    
    // Signal the thread to wake up
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    
    if (listenerThread_.joinable()) {
        listenerThread_.join();
    }
}

void UdpListener::setMessageHandler(MessageHandler handler) {
    messageHandler_ = std::move(handler);
}

void UdpListener::run() {
    char buffer[Config::BUFFER_SIZE];
    sockaddr_in clientAddr{};
    int clientAddrLen = sizeof(clientAddr);
	
	// Service mode - no console output
    // std::cout << "UDP listener thread started" << std::endl;
    
    while (running_) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket_, &readSet);
        
        timeval timeout{};
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        
        int result = select(0, &readSet, nullptr, nullptr, &timeout);
        
        if (result == SOCKET_ERROR) {
            if (running_) {
                std::cerr << "Select failed: " << WSAGetLastError() << std::endl;
            }
            break;
        }
        
        if (result > 0 && FD_ISSET(socket_, &readSet)) {
            int bytesReceived = recvfrom(socket_, buffer, sizeof(buffer) - 1, 0,
                                       reinterpret_cast<sockaddr*>(&clientAddr), &clientAddrLen);
            
            if (bytesReceived == SOCKET_ERROR) {
                int error = WSAGetLastError();
                if (error != WSAEWOULDBLOCK && running_) {
                    std::cerr << "recvfrom failed: " << error << std::endl;
                }
                continue;
            }
            
            if (bytesReceived > 0) {
                buffer[bytesReceived] = '\0'; // Null-terminate the string
                std::string message(buffer);
                
                // Usuń znaki nowej linii i powrotu karetki
                message.erase(std::remove(message.begin(), message.end(), '\r'), message.end());
                message.erase(std::remove(message.begin(), message.end(), '\n'), message.end());
                
                if (!message.empty()) {
                    std::string clientInfo = getClientInfo(clientAddr);

                    // Service mode - no console output
                    // std::cout << "Received message from " << clientInfo << ": " << message << std::endl;
                    
                    if (messageHandler_) {
                        messageHandler_(message, clientInfo);
                    }
                }
            }
        }
    }

    // Service mode - no console output
    // std::cout << "UDP listener thread stopped" << std::endl;
}

void UdpListener::cleanup() {
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

std::string UdpListener::getClientInfo(const sockaddr_in& clientAddr) const {
    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, sizeof(ipStr));
    
    return std::string(ipStr) + ":" + std::to_string(ntohs(clientAddr.sin_port));
}