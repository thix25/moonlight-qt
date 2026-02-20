#pragma once

#include <QObject>
#include <QMap>
#include <QString>
#include <QSettings>
#include <QVariantList>
#include <QReadWriteLock>

// Value -1 means "Automatic" (first available slot), 0-15 map to player indices
#define GAMEPAD_MAPPING_AUTO -1
#define GAMEPAD_MAPPING_MAX_PLAYERS 4

class GamepadMapping : public QObject
{
    Q_OBJECT

public:
    // Get the singleton instance
    static GamepadMapping* get();

    // Set a GUID -> player index mapping in global settings
    // playerIndex: -1 = auto, 0 = Player 1, 1 = Player 2, etc.
    Q_INVOKABLE void setGlobalMapping(const QString& guid, int playerIndex);

    // Remove a global mapping for a GUID
    Q_INVOKABLE void removeGlobalMapping(const QString& guid);

    // Get the global mapping for a GUID (-1 if not set / auto)
    Q_INVOKABLE int getGlobalMapping(const QString& guid) const;

    // Set a GUID -> player index mapping for a specific client
    Q_INVOKABLE void setClientMapping(const QString& clientUuid, const QString& guid, int playerIndex);

    // Remove a client mapping for a GUID
    Q_INVOKABLE void removeClientMapping(const QString& clientUuid, const QString& guid);

    // Get the client mapping for a GUID (-1 if not set / auto)
    Q_INVOKABLE int getClientMapping(const QString& clientUuid, const QString& guid) const;

    // Check if client-specific mappings are enabled for this client
    Q_INVOKABLE bool hasClientMappings(const QString& clientUuid) const;

    // Enable/disable per-client mapping override
    Q_INVOKABLE void setClientMappingEnabled(const QString& clientUuid, bool enabled);

    // Check if per-client mapping override is enabled
    Q_INVOKABLE bool isClientMappingEnabled(const QString& clientUuid) const;

    // Reset all client mappings for a given client
    Q_INVOKABLE void resetClientMappings(const QString& clientUuid);

    // Resolve the effective player index for a GUID, considering client override
    // Returns -1 for auto, 0-3 for specific player index
    int resolveMapping(const QString& clientUuid, const QString& guid) const;

    // Get all global mappings as a map of GUID -> playerIndex
    QMap<QString, int> getAllGlobalMappings() const;

    // Get all client mappings for a specific client
    QMap<QString, int> getAllClientMappings(const QString& clientUuid) const;

    // Returns a list of currently connected gamepad info for the QML UI
    // Each entry is a QVariantMap with keys: "name", "guid", "index"
    Q_INVOKABLE QVariantList getConnectedGamepads() const;

    // Save all pending changes
    Q_INVOKABLE void save();

    // Reload from disk
    Q_INVOKABLE void reload();

private:
    explicit GamepadMapping();

    // Internal save without acquiring lock (caller must hold write lock)
    void saveLocked();

    // Global mappings: GUID -> player index
    QMap<QString, int> m_GlobalMappings;

    // Per-client mappings: clientUuid -> (GUID -> player index)
    QMap<QString, QMap<QString, int>> m_ClientMappings;

    // Per-client enabled flags
    QMap<QString, bool> m_ClientMappingEnabled;

    // Thread-safety lock for cross-thread access (QML thread vs streaming thread)
    mutable QReadWriteLock m_Lock;

    static GamepadMapping* s_Instance;
};
