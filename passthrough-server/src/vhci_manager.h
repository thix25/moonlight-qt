#pragma once

#include <string>
#include <cstdint>

// Stub for Phase 2 VHCI driver management
class VhciManager {
public:
    VhciManager() = default;
    ~VhciManager() = default;

    // Check if the usbip-win VHCI driver is loaded
    bool isDriverAvailable() const;

    // Create a virtual USB port for a device
    // Returns the assigned port number, or -1 on failure
    int attachDevice(uint32_t deviceId, uint16_t vendorId, uint16_t productId);

    // Remove a virtual USB port
    bool detachDevice(uint32_t deviceId);

    // Get the number of currently attached devices
    int getAttachedCount() const;
};
