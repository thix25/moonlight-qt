#include "vhci_manager.h"

#include <cstdio>

// Phase 2 stub implementation
// Will be replaced with actual usbip-win VHCI driver interaction

bool VhciManager::isDriverAvailable() const
{
    // TODO Phase 2: Check if usbip_vhci.sys is loaded
    // Use CreateFile on \\.\USBIP_VHCI or similar
    printf("[VHCI] Driver availability check: not yet implemented\n");
    return false;
}

int VhciManager::attachDevice(uint32_t deviceId, uint16_t vendorId, uint16_t productId)
{
    // TODO Phase 2: Create virtual USB port via VHCI driver IOCTL
    printf("[VHCI] Attach device %u (VID:%04X PID:%04X): not yet implemented\n",
           deviceId, vendorId, productId);
    return -1;
}

bool VhciManager::detachDevice(uint32_t deviceId)
{
    // TODO Phase 2: Remove virtual USB port
    printf("[VHCI] Detach device %u: not yet implemented\n", deviceId);
    return false;
}

int VhciManager::getAttachedCount() const
{
    return 0;
}
