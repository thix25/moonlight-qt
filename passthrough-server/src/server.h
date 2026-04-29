#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#endif

#include "protocol.h"
#include "vhci_manager.h"

struct ClientConnection {
    SOCKET socket;
    std::string address;
    std::thread thread;
    std::atomic<bool> running;
    uint8_t sessionId[16];

    ClientConnection() : socket(INVALID_SOCKET), running(false) {
        memset(sessionId, 0, sizeof(sessionId));
    }
};

struct ServerConfig {
    uint16_t port = MlptProtocol::DEFAULT_PORT;
    bool vhciAvailable = false;
};

class PassthroughServer {
public:
    PassthroughServer();
    ~PassthroughServer();

    bool start(const ServerConfig& config);
    void stop();
    bool isRunning() const { return m_Running; }

    using LogCallback = std::function<void(const std::string&)>;
    void setLogCallback(LogCallback cb) { m_LogCallback = cb; }

private:
    void acceptLoop();
    void clientLoop(ClientConnection* client);

    bool sendMessage(SOCKET sock, MlptProtocol::MsgType type,
                     const void* payload = nullptr, uint32_t payloadLen = 0);
    bool recvExact(SOCKET sock, void* buf, int len);

    void processMessage(ClientConnection* client,
                        const MlptProtocol::Header& header,
                        const std::vector<uint8_t>& payload);

    void handleHello(ClientConnection* client, const std::vector<uint8_t>& payload);
    void handleDeviceList(ClientConnection* client, const std::vector<uint8_t>& payload);
    void handleDeviceAttach(ClientConnection* client, const std::vector<uint8_t>& payload);
    void handleDeviceDetach(ClientConnection* client, const std::vector<uint8_t>& payload);
    void handleUsbIpReturn(ClientConnection* client, const std::vector<uint8_t>& payload);

    void log(const std::string& msg);

    SOCKET m_ListenSocket;
    std::thread m_AcceptThread;
    std::atomic<bool> m_Running;
    ServerConfig m_Config;

    std::mutex m_ClientsMutex;
    std::vector<std::unique_ptr<ClientConnection>> m_Clients;

    VhciManager m_VhciManager;

    LogCallback m_LogCallback;
};
