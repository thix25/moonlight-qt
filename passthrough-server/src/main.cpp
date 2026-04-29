// Moonlight Passthrough Server - Companion service for USB/BT device forwarding
// Runs alongside Sunshine on the gaming PC (server side)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <csignal>

#ifdef _WIN32
#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include "server.h"
#include "vhci_manager.h"

static PassthroughServer* g_Server = nullptr;

void signalHandler(int sig)
{
    (void)sig;
    printf("\nShutting down...\n");
    if (g_Server) {
        g_Server->stop();
    }
}

void printUsage(const char* argv0)
{
    printf("Moonlight Passthrough Server v%d.%d\n",
           MlptProtocol::VERSION >> 8, MlptProtocol::VERSION & 0xFF);
    printf("Companion service for USB/Bluetooth device forwarding.\n");
    printf("Run alongside Sunshine on the gaming PC.\n\n");
    printf("Usage: %s [options]\n\n", argv0);
    printf("Options:\n");
    printf("  --port N     Listen port (default: %d)\n", MlptProtocol::DEFAULT_PORT);
    printf("  --help       Show this help\n");
    printf("\nThe server listens for connections from Moonlight clients and\n");
    printf("creates virtual USB devices via the VHCI driver.\n");
}

int main(int argc, char* argv[])
{
    uint16_t port = MlptProtocol::DEFAULT_PORT;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    // Initialize Winsock
#ifdef _WIN32
    WSADATA wsaData;
    int wsaErr = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (wsaErr != 0) {
        fprintf(stderr, "WSAStartup failed: %d\n", wsaErr);
        return 1;
    }
#endif

    // Start server (VhciManager is created internally)
    PassthroughServer server;
    g_Server = &server;

    ServerConfig config;
    config.port = port;

    printf("===========================================\n");
    printf(" Moonlight Passthrough Server v%d.%d\n",
           MlptProtocol::VERSION >> 8, MlptProtocol::VERSION & 0xFF);
    printf("===========================================\n");
    printf(" Port: %d\n", port);
    printf("===========================================\n\n");

    // Set up signal handler
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    if (!server.start(config)) {
        fprintf(stderr, "Failed to start server\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("Server running. Press Ctrl+C to stop.\n\n");

    // Wait until stopped
    while (server.isRunning()) {
#ifdef _WIN32
        Sleep(500);
#else
        usleep(500000);
#endif
    }

    g_Server = nullptr;

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
