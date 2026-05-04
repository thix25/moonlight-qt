#pragma once

#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>
#include <memory>
#include <unordered_map>

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
    std::mutex sendMutex;  // Protects multi-part sends on the socket

    ClientConnection() : socket(INVALID_SOCKET), running(false) {
        memset(sessionId, 0, sizeof(sessionId));
    }
};

struct ServerConfig {
    uint16_t port = MlptProtocol::DEFAULT_PORT;
    bool vhciAvailable = false;
    VhciBackendType forceBackend = VhciBackendType::WIN2;  // default: try win2 first
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

    // Status info for system tray
    int getClientCount() const;
    int getDeviceCount() const;

    using StatusCallback = std::function<void(int clients, int devices)>;
    void setStatusCallback(StatusCallback cb) { m_StatusCallback = cb; }

private:
    void acceptLoop();
    void clientLoop(ClientConnection* client);
    void cleanupDisconnectedClients();

    bool sendMessage(ClientConnection* client, MlptProtocol::MsgType type,
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

    // Convert native VHCI URB to our protocol format and send to client
    void forwardVhciUrbToClient(uint32_t deviceId,
                                const uint8_t* nativeData, size_t nativeLen);

    void log(const std::string& msg);
    void notifyStatusChange();

    SOCKET m_ListenSocket;
    std::thread m_AcceptThread;
    std::atomic<bool> m_Running;
    ServerConfig m_Config;

    mutable std::mutex m_ClientsMutex;
    std::vector<std::unique_ptr<ClientConnection>> m_Clients;

    // Maps deviceId -> ClientConnection* that owns it
    mutable std::mutex m_DeviceOwnersMutex;
    std::unordered_map<uint32_t, ClientConnection*> m_DeviceOwners;

    std::unique_ptr<VhciManager> m_VhciManager;

    LogCallback m_LogCallback;
    StatusCallback m_StatusCallback;
};
