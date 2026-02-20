#include "appmodel.h"
#include "settings/streamingpreferences.h"

#include <QSettings>
#include <algorithm>

AppModel::AppModel(QObject *parent)
    : QAbstractListModel(parent),
      m_Computer(nullptr),
      m_ComputerManager(nullptr),
      m_CurrentGameId(0),
      m_ShowHiddenGames(false),
      m_SortMode(0)
{
    connect(&m_BoxArtManager, &BoxArtManager::boxArtLoadComplete,
            this, &AppModel::handleBoxArtLoaded);
}

void AppModel::initialize(ComputerManager* computerManager, int computerIndex, bool showHiddenGames)
{
    m_ComputerManager = computerManager;
    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &AppModel::handleComputerStateChanged);

    Q_ASSERT(computerIndex < m_ComputerManager->getComputers().count());
    m_Computer = m_ComputerManager->getComputers().at(computerIndex);
    m_CurrentGameId = m_Computer->currentGameId;
    m_ShowHiddenGames = showHiddenGames;

    // Load sort mode from preferences
    StreamingPreferences* prefs = StreamingPreferences::get();
    m_SortMode = static_cast<int>(prefs->appSortMode);

    // Load custom order
    loadCustomOrder();

    updateAppList(m_Computer->appList);
}

int AppModel::getRunningAppId()
{
    return m_CurrentGameId;
}

QString AppModel::getRunningAppName()
{
    if (m_CurrentGameId != 0) {
        for (int i = 0; i < m_AllApps.count(); i++) {
            if (m_AllApps[i].id == m_CurrentGameId) {
                return m_AllApps[i].name;
            }
        }
    }

    return nullptr;
}

QString AppModel::getAppName(int appIndex) const
{
    if (appIndex >= 0 && appIndex < m_VisibleApps.count()) {
        return m_VisibleApps.at(appIndex).name;
    }
    return QString();
}

int AppModel::getAppId(int appIndex) const
{
    if (appIndex >= 0 && appIndex < m_VisibleApps.count()) {
        return m_VisibleApps.at(appIndex).id;
    }
    return 0;
}

Session* AppModel::createSessionForApp(int appIndex)
{
    Q_ASSERT(appIndex < m_VisibleApps.count());
    NvApp app = m_VisibleApps.at(appIndex);

    return new Session(m_Computer, app);
}

int AppModel::getDirectLaunchAppIndex()
{
    for (int i = 0; i < m_VisibleApps.count(); i++) {
        if (m_VisibleApps[i].directLaunch) {
            return i;
        }
    }

    return -1;
}

int AppModel::rowCount(const QModelIndex &parent) const
{
    // For list models only the root node (an invalid parent) should return the list's size. For all
    // other (valid) parents, rowCount() should return 0 so that it does not become a tree model.
    if (parent.isValid())
        return 0;

    return m_VisibleApps.count();
}

QVariant AppModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return QVariant();

    Q_ASSERT(index.row() < m_VisibleApps.count());
    NvApp app = m_VisibleApps.at(index.row());

    switch (role)
    {
    case NameRole:
        return app.name;
    case RunningRole:
        return m_Computer->currentGameId == app.id;
    case BoxArtRole:
        // FIXME: const-correctness
        return const_cast<BoxArtManager&>(m_BoxArtManager).loadBoxArt(m_Computer, app);
    case HiddenRole:
        return app.hidden;
    case AppIdRole:
        return app.id;
    case DirectLaunchRole:
        return app.directLaunch;
    case AppCollectorGameRole:
        return app.isAppCollectorGame;
    case FolderRole: {
        StreamingPreferences* prefs = StreamingPreferences::get();
        QString folder = prefs->getAppFolder(m_Computer->uuid, QString::number(app.id));
        return folder;
    }
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> AppModel::roleNames() const
{
    QHash<int, QByteArray> names;

    names[NameRole] = "name";
    names[RunningRole] = "running";
    names[BoxArtRole] = "boxart";
    names[HiddenRole] = "hidden";
    names[AppIdRole] = "appid";
    names[DirectLaunchRole] = "directLaunch";
    names[AppCollectorGameRole] = "appCollectorGame";
    names[FolderRole] = "folder";

    return names;
}

void AppModel::quitRunningApp()
{
    m_ComputerManager->quitRunningApp(m_Computer);
}

bool AppModel::isAppCurrentlyVisible(const NvApp& app)
{
    for (const NvApp& visibleApp : m_VisibleApps) {
        if (app.id == visibleApp.id) {
            return true;
        }
    }

    return false;
}

QVector<NvApp> AppModel::getVisibleApps(const QVector<NvApp>& appList)
{
    QVector<NvApp> visibleApps;

    for (const NvApp& app : appList) {
        // Don't immediately hide games that were previously visible. This
        // allows users to easily uncheck the "Hide App" checkbox if they
        // check it by mistake.
        if (m_ShowHiddenGames || !app.hidden || isAppCurrentlyVisible(app)) {
            visibleApps.append(app);
        }
    }

    return visibleApps;
}

void AppModel::updateAppList(QVector<NvApp> newList)
{
    m_AllApps = newList;

    QVector<NvApp> newVisibleList = getVisibleApps(newList);

    // Apply folder filter if we're inside a folder
    if (!m_CurrentFolder.isEmpty()) {
        StreamingPreferences* prefs = StreamingPreferences::get();
        QStringList folderApps = prefs->getAppsInFolder(m_Computer->uuid, m_CurrentFolder);
        QVector<NvApp> filtered;
        for (const NvApp& app : newVisibleList) {
            if (folderApps.contains(QString::number(app.id))) {
                filtered.append(app);
            }
        }
        newVisibleList = filtered;
    }

    // Sort the list according to current mode
    if (m_SortMode == 1 && !m_CustomOrder.isEmpty()) {
        // Custom sort: order by position in m_CustomOrder
        std::sort(newVisibleList.begin(), newVisibleList.end(),
                  [this](const NvApp& a, const NvApp& b) {
            int idxA = m_CustomOrder.indexOf(QString::number(a.id));
            int idxB = m_CustomOrder.indexOf(QString::number(b.id));
            // Apps not in custom order go to the end, alphabetically
            if (idxA < 0 && idxB < 0) return a.name.toLower() < b.name.toLower();
            if (idxA < 0) return false;
            if (idxB < 0) return true;
            return idxA < idxB;
        });
    } else {
        // Alphabetical sort (default)
        std::sort(newVisibleList.begin(), newVisibleList.end(),
                  [](const NvApp& a, const NvApp& b) {
            return a.name.toLower() < b.name.toLower();
        });
    }

    // Full model reset for simplicity
    beginResetModel();
    m_VisibleApps = newVisibleList;
    endResetModel();
}

void AppModel::sortVisibleApps()
{
    updateAppList(m_AllApps);
}

void AppModel::setSortMode(int mode)
{
    if (m_SortMode != mode) {
        m_SortMode = mode;

        // Persist to preferences
        StreamingPreferences* prefs = StreamingPreferences::get();
        prefs->appSortMode = static_cast<StreamingPreferences::AppSortMode>(mode);
        prefs->save();

        // Re-sort the list
        sortVisibleApps();
    }
}

int AppModel::getSortMode() const
{
    return m_SortMode;
}

void AppModel::moveApp(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_VisibleApps.count() ||
        toIndex < 0 || toIndex >= m_VisibleApps.count() ||
        fromIndex == toIndex) {
        return;
    }

    // Switch to custom mode if not already
    if (m_SortMode != 1) {
        m_SortMode = 1;
        StreamingPreferences* prefs = StreamingPreferences::get();
        prefs->appSortMode = StreamingPreferences::ASM_CUSTOM;
        prefs->save();
    }

    // Perform the move in the model
    int destIndex = toIndex > fromIndex ? toIndex + 1 : toIndex;
    beginMoveRows(QModelIndex(), fromIndex, fromIndex, QModelIndex(), destIndex);
    m_VisibleApps.move(fromIndex, toIndex);
    endMoveRows();

    // Update and save custom order
    m_CustomOrder.clear();
    for (const NvApp& app : m_VisibleApps) {
        m_CustomOrder.append(QString::number(app.id));
    }
    saveCustomOrder();
}

QString AppModel::getComputerUuid() const
{
    if (m_Computer) {
        return m_Computer->uuid;
    }
    return QString();
}

void AppModel::setCurrentFolder(const QString& folderName)
{
    if (m_CurrentFolder != folderName) {
        m_CurrentFolder = folderName;
        sortVisibleApps();
    }
}

QString AppModel::getCurrentFolder() const
{
    return m_CurrentFolder;
}

void AppModel::saveCustomOrder()
{
    if (m_Computer) {
        StreamingPreferences* prefs = StreamingPreferences::get();
        prefs->setAppCustomOrder(m_Computer->uuid, m_CustomOrder);
    }
}

void AppModel::loadCustomOrder()
{
    if (m_Computer) {
        StreamingPreferences* prefs = StreamingPreferences::get();
        m_CustomOrder = prefs->getAppCustomOrder(m_Computer->uuid);
    }
}

void AppModel::setAppHidden(int appIndex, bool hidden)
{
    Q_ASSERT(appIndex < m_VisibleApps.count());
    int appId = m_VisibleApps.at(appIndex).id;

    {
        QWriteLocker lock(&m_Computer->lock);

        for (NvApp& app : m_Computer->appList) {
            if (app.id == appId) {
                app.hidden = hidden;
                break;
            }
        }
    }

    m_ComputerManager->clientSideAttributeUpdated(m_Computer);
}

void AppModel::setAppDirectLaunch(int appIndex, bool directLaunch)
{
    Q_ASSERT(appIndex < m_VisibleApps.count());
    int appId = m_VisibleApps.at(appIndex).id;

    {
        QWriteLocker lock(&m_Computer->lock);

        for (NvApp& app : m_Computer->appList) {
            if (directLaunch) {
                // We must clear direct launch from all other apps
                // to set it on the new app.
                app.directLaunch = app.id == appId;
            }
            else if (app.id == appId) {
                // If we're clearing direct launch, we're done once we
                // find our matching app ID.
                app.directLaunch = false;
                break;
            }
        }
    }

    m_ComputerManager->clientSideAttributeUpdated(m_Computer);
}

void AppModel::handleComputerStateChanged(NvComputer* computer)
{
    // Ignore updates for computers that aren't ours
    if (computer != m_Computer) {
        return;
    }

    // If the computer has gone offline or we've been unpaired,
    // signal the UI so we can go back to the PC view.
    if (m_Computer->state == NvComputer::CS_OFFLINE ||
            m_Computer->pairState == NvComputer::PS_NOT_PAIRED) {
        emit computerLost();
        return;
    }

    // First, process additions/removals from the app list. This
    // is required because the new game may now be running, so
    // we can't check that first.
    if (computer->appList != m_AllApps) {
        updateAppList(computer->appList);
    }

    // Finally, process changes to the active app
    if (computer->currentGameId != m_CurrentGameId) {
        // First, invalidate the running state of newly running game
        for (int i = 0; i < m_VisibleApps.count(); i++) {
            if (m_VisibleApps[i].id == computer->currentGameId) {
                emit dataChanged(createIndex(i, 0),
                                 createIndex(i, 0),
                                 QVector<int>() << RunningRole);
                break;
            }
        }

        // Next, invalidate the running state of the old game (if it exists)
        if (m_CurrentGameId != 0) {
            for (int i = 0; i < m_VisibleApps.count(); i++) {
                if (m_VisibleApps[i].id == m_CurrentGameId) {
                    emit dataChanged(createIndex(i, 0),
                                     createIndex(i, 0),
                                     QVector<int>() << RunningRole);
                    break;
                }
            }
        }

        // Now update our internal state
        m_CurrentGameId = m_Computer->currentGameId;
    }
}

void AppModel::handleBoxArtLoaded(NvComputer* computer, NvApp app, QUrl /* image */)
{
    Q_ASSERT(computer == m_Computer);

    int index = m_VisibleApps.indexOf(app);

    // Make sure we're not delivering a callback to an app that's already been removed
    if (index >= 0) {
        // Let our view know the box art data has changed for this app
        emit dataChanged(createIndex(index, 0),
                         createIndex(index, 0),
                         QVector<int>() << BoxArtRole);
    }
    else {
        qWarning() << "App not found for box art callback:" << app.name;
    }
}
