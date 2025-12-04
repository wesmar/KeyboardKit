#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <functional>
#include <thread>
#include <atomic>

class UdpListener {
public:
    using MessageHandler = std::function<void(const std::string&, const std::string&)>;
    
    UdpListener(const std::string& address, int port);
    ~UdpListener();
    
    bool start();
    void stop();
    void setMessageHandler(MessageHandler handler);
    
private:
    void run();
    void cleanup();
    std::string getClientInfo(const sockaddr_in& clientAddr) const;
    
    std::string address_;
    int port_;
    SOCKET socket_;
    std::thread listenerThread_;
    std::atomic<bool> running_;
    MessageHandler messageHandler_;
};