#include "gamepadmapping.h"

#include <QSettings>
#include <QtDebug>
#include <QVariantMap>

#include "SDL_compat.h"

#define SER_GAMEPADMAPPING_GROUP "gamepadmappings"
#define SER_GAMEPADMAPPING_GLOBAL "global"
#define SER_GAMEPADMAPPING_CLIENTS "clients"
#define SER_GAMEPADMAPPING_ENABLED "enabled"
#define SER_GAMEPADMAPPING_MAPPINGS "mappings"

GamepadMapping* GamepadMapping::s_Instance = nullptr;

GamepadMapping::GamepadMapping()
{
    reload();
}

GamepadMapping* GamepadMapping::get()
{
    if (!s_Instance) {
        s_Instance = new GamepadMapping();
    }
    return s_Instance;
}

void GamepadMapping::setGlobalMapping(const QString& guid, int playerIndex)
{
    QWriteLocker lock(&m_Lock);
    if (playerIndex == GAMEPAD_MAPPING_AUTO) {
        m_GlobalMappings.remove(guid);
    } else {
        m_GlobalMappings[guid] = qBound(0, playerIndex, GAMEPAD_MAPPING_MAX_PLAYERS - 1);
    }
    saveLocked();
}

void GamepadMapping::removeGlobalMapping(const QString& guid)
{
    QWriteLocker lock(&m_Lock);
    m_GlobalMappings.remove(guid);
    saveLocked();
}

int GamepadMapping::getGlobalMapping(const QString& guid) const
{
    QReadLocker lock(&m_Lock);
    return m_GlobalMappings.value(guid, GAMEPAD_MAPPING_AUTO);
}

void GamepadMapping::setClientMapping(const QString& clientUuid, const QString& guid, int playerIndex)
{
    if (clientUuid.isEmpty()) return;

    QWriteLocker lock(&m_Lock);
    if (playerIndex == GAMEPAD_MAPPING_AUTO) {
        m_ClientMappings[clientUuid].remove(guid);
    } else {
        m_ClientMappings[clientUuid][guid] = qBound(0, playerIndex, GAMEPAD_MAPPING_MAX_PLAYERS - 1);
    }
    saveLocked();
}

void GamepadMapping::removeClientMapping(const QString& clientUuid, const QString& guid)
{
    if (clientUuid.isEmpty()) return;

    QWriteLocker lock(&m_Lock);
    if (m_ClientMappings.contains(clientUuid)) {
        m_ClientMappings[clientUuid].remove(guid);
    }
    saveLocked();
}

int GamepadMapping::getClientMapping(const QString& clientUuid, const QString& guid) const
{
    QReadLocker lock(&m_Lock);
    if (clientUuid.isEmpty() || !m_ClientMappings.contains(clientUuid)) {
        return GAMEPAD_MAPPING_AUTO;
    }
    return m_ClientMappings[clientUuid].value(guid, GAMEPAD_MAPPING_AUTO);
}

bool GamepadMapping::hasClientMappings(const QString& clientUuid) const
{
    QReadLocker lock(&m_Lock);
    if (clientUuid.isEmpty()) return false;
    return m_ClientMappings.contains(clientUuid) && !m_ClientMappings[clientUuid].isEmpty();
}

void GamepadMapping::setClientMappingEnabled(const QString& clientUuid, bool enabled)
{
    if (clientUuid.isEmpty()) return;

    QWriteLocker lock(&m_Lock);
    m_ClientMappingEnabled[clientUuid] = enabled;
    saveLocked();
}

bool GamepadMapping::isClientMappingEnabled(const QString& clientUuid) const
{
    QReadLocker lock(&m_Lock);
    if (clientUuid.isEmpty()) return false;
    return m_ClientMappingEnabled.value(clientUuid, false);
}

void GamepadMapping::resetClientMappings(const QString& clientUuid)
{
    if (clientUuid.isEmpty()) return;

    QWriteLocker lock(&m_Lock);
    m_ClientMappings.remove(clientUuid);
    m_ClientMappingEnabled.remove(clientUuid);
    saveLocked();
}

int GamepadMapping::resolveMapping(const QString& clientUuid, const QString& guid) const
{
    QReadLocker lock(&m_Lock);

    // If client-specific mappings are enabled for this client, try client mapping first
    if (!clientUuid.isEmpty() && m_ClientMappingEnabled.value(clientUuid, false)) {
        if (m_ClientMappings.contains(clientUuid)) {
            int clientMapping = m_ClientMappings[clientUuid].value(guid, GAMEPAD_MAPPING_AUTO);
            if (clientMapping != GAMEPAD_MAPPING_AUTO) {
                return clientMapping;
            }
        }
    }

    // Fall back to global mapping
    return m_GlobalMappings.value(guid, GAMEPAD_MAPPING_AUTO);
}

QMap<QString, int> GamepadMapping::getAllGlobalMappings() const
{
    QReadLocker lock(&m_Lock);
    return m_GlobalMappings;
}

QMap<QString, int> GamepadMapping::getAllClientMappings(const QString& clientUuid) const
{
    QReadLocker lock(&m_Lock);
    if (clientUuid.isEmpty() || !m_ClientMappings.contains(clientUuid)) {
        return QMap<QString, int>();
    }
    return m_ClientMappings[clientUuid];
}

QVariantList GamepadMapping::getConnectedGamepads() const
{
    QVariantList result;

    // Temporarily init SDL joystick and gamecontroller if not already init
    bool needInitJoystick = !SDL_WasInit(SDL_INIT_JOYSTICK);
    bool needInitGC = !SDL_WasInit(SDL_INIT_GAMECONTROLLER);

    if (needInitJoystick) {
        if (SDL_InitSubSystem(SDL_INIT_JOYSTICK) != 0) {
            qWarning() << "Failed to init SDL joystick subsystem:" << SDL_GetError();
            return result;
        }
    }
    if (needInitGC) {
        if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            qWarning() << "Failed to init SDL gamecontroller subsystem:" << SDL_GetError();
            if (needInitJoystick) SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
            return result;
        }
    }

    int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < numJoysticks; i++) {
        if (SDL_IsGameController(i)) {
            char guidStr[33];
            SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(i),
                                      guidStr, sizeof(guidStr));
            const char* name = SDL_GameControllerNameForIndex(i);

            QVariantMap entry;
            entry["name"] = name ? QString(name) : QStringLiteral("<Unknown Controller>");
            entry["guid"] = QString(guidStr);
            entry["index"] = i;
            result.append(entry);
        }
    }

    if (needInitGC) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
    if (needInitJoystick) {
        SDL_QuitSubSystem(SDL_INIT_JOYSTICK);
    }

    return result;
}

void GamepadMapping::save()
{
    QWriteLocker lock(&m_Lock);
    saveLocked();
}

void GamepadMapping::saveLocked()
{
    QSettings settings;

    // Clear existing gamepad mapping group
    settings.beginGroup(SER_GAMEPADMAPPING_GROUP);
    settings.remove("");

    // Save global mappings
    settings.beginGroup(SER_GAMEPADMAPPING_GLOBAL);
    for (auto it = m_GlobalMappings.constBegin(); it != m_GlobalMappings.constEnd(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup(); // global

    // Save client mappings
    settings.beginGroup(SER_GAMEPADMAPPING_CLIENTS);
    for (auto clientIt = m_ClientMappings.constBegin(); clientIt != m_ClientMappings.constEnd(); ++clientIt) {
        settings.beginGroup(clientIt.key());

        // Save enabled flag
        settings.setValue(SER_GAMEPADMAPPING_ENABLED,
                          m_ClientMappingEnabled.value(clientIt.key(), false));

        // Save mappings
        settings.beginGroup(SER_GAMEPADMAPPING_MAPPINGS);
        for (auto mappingIt = clientIt.value().constBegin(); mappingIt != clientIt.value().constEnd(); ++mappingIt) {
            settings.setValue(mappingIt.key(), mappingIt.value());
        }
        settings.endGroup(); // mappings
        settings.endGroup(); // clientUuid
    }

    // Also save enabled flags for clients that have the flag but no mappings yet
    for (auto enabledIt = m_ClientMappingEnabled.constBegin(); enabledIt != m_ClientMappingEnabled.constEnd(); ++enabledIt) {
        if (!m_ClientMappings.contains(enabledIt.key())) {
            settings.beginGroup(enabledIt.key());
            settings.setValue(SER_GAMEPADMAPPING_ENABLED, enabledIt.value());
            settings.endGroup();
        }
    }

    settings.endGroup(); // clients
    settings.endGroup(); // gamepadmappings
}

void GamepadMapping::reload()
{
    QWriteLocker lock(&m_Lock);

    QSettings settings;

    m_GlobalMappings.clear();
    m_ClientMappings.clear();
    m_ClientMappingEnabled.clear();

    settings.beginGroup(SER_GAMEPADMAPPING_GROUP);

    // Load global mappings
    settings.beginGroup(SER_GAMEPADMAPPING_GLOBAL);
    for (const QString& guid : settings.childKeys()) {
        m_GlobalMappings[guid] = settings.value(guid, GAMEPAD_MAPPING_AUTO).toInt();
    }
    settings.endGroup(); // global

    // Load client mappings
    settings.beginGroup(SER_GAMEPADMAPPING_CLIENTS);
    for (const QString& clientUuid : settings.childGroups()) {
        settings.beginGroup(clientUuid);

        m_ClientMappingEnabled[clientUuid] = settings.value(SER_GAMEPADMAPPING_ENABLED, false).toBool();

        settings.beginGroup(SER_GAMEPADMAPPING_MAPPINGS);
        QMap<QString, int> clientMap;
        for (const QString& guid : settings.childKeys()) {
            clientMap[guid] = settings.value(guid, GAMEPAD_MAPPING_AUTO).toInt();
        }
        if (!clientMap.isEmpty()) {
            m_ClientMappings[clientUuid] = clientMap;
        }
        settings.endGroup(); // mappings
        settings.endGroup(); // clientUuid
    }
    settings.endGroup(); // clients

    settings.endGroup(); // gamepadmappings

    qInfo() << "Loaded gamepad mappings:" << m_GlobalMappings.count() << "global,"
            << m_ClientMappings.count() << "clients";
}
