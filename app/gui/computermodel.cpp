#include "computermodel.h"
#include "settings/streamingpreferences.h"

#include <QThreadPool>
#include <QSettings>
#include <algorithm>

ComputerModel::ComputerModel(QObject* object)
    : QAbstractListModel(object), m_ComputerManager(nullptr), m_SortMode(0) {}

void ComputerModel::initialize(ComputerManager* computerManager)
{
    m_ComputerManager = computerManager;
    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &ComputerModel::handleComputerStateChanged);
    connect(m_ComputerManager, &ComputerManager::pairingCompleted,
            this, &ComputerModel::handlePairingCompleted);

    // Load sort mode from preferences
    StreamingPreferences* prefs = StreamingPreferences::get();
    m_SortMode = static_cast<int>(prefs->pcSortMode);

    // Load custom order
    loadCustomOrder();

    m_Computers = m_ComputerManager->getComputers();
    sortComputers();
}

QVariant ComputerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    Q_ASSERT(index.row() < m_Computers.count());

    NvComputer* computer = m_Computers[index.row()];
    QReadLocker lock(&computer->lock);

    switch (role) {
    case NameRole:
        return computer->name;
    case OnlineRole:
        return computer->state == NvComputer::CS_ONLINE;
    case PairedRole:
        return computer->pairState == NvComputer::PS_PAIRED;
    case BusyRole:
        return computer->currentGameId != 0;
    case WakeableRole:
        return !computer->macAddress.isEmpty();
    case StatusUnknownRole:
        return computer->state == NvComputer::CS_UNKNOWN;
    case ServerSupportedRole:
        return computer->isSupportedServerVersion;
    case DetailsRole: {
        QString state, pairState;

        switch (computer->state) {
        case NvComputer::CS_ONLINE:
            state = tr("Online");
            break;
        case NvComputer::CS_OFFLINE:
            state = tr("Offline");
            break;
        default:
            state = tr("Unknown");
            break;
        }

        switch (computer->pairState) {
        case NvComputer::PS_PAIRED:
            pairState = tr("Paired");
            break;
        case NvComputer::PS_NOT_PAIRED:
            pairState = tr("Unpaired");
            break;
        default:
            pairState = tr("Unknown");
            break;
        }

        return tr("Name: %1").arg(computer->name) + '\n' +
               tr("Status: %1").arg(state) + '\n' +
               tr("Active Address: %1").arg(computer->activeAddress.toString()) + '\n' +
               tr("UUID: %1").arg(computer->uuid) + '\n' +
               tr("Local Address: %1").arg(computer->localAddress.toString()) + '\n' +
               tr("Remote Address: %1").arg(computer->remoteAddress.toString()) + '\n' +
               tr("IPv6 Address: %1").arg(computer->ipv6Address.toString()) + '\n' +
               tr("Manual Address: %1").arg(computer->manualAddress.toString()) + '\n' +
               tr("MAC Address: %1").arg(computer->macAddress.isEmpty() ? tr("Unknown") : QString(computer->macAddress.toHex(':'))) + '\n' +
               tr("Pair State: %1").arg(pairState) + '\n' +
               tr("Running Game ID: %1").arg(computer->state == NvComputer::CS_ONLINE ? QString::number(computer->currentGameId) : tr("Unknown")) + '\n' +
               tr("HTTPS Port: %1").arg(computer->state == NvComputer::CS_ONLINE ? QString::number(computer->activeHttpsPort) : tr("Unknown"));
    }
    case UuidRole:
        return computer->uuid;
    case SectionRole: {
        // Return cached section from last sort to prevent race conditions
        // between sorting and data display
        return m_CachedSections.value(computer->uuid, tr("Offline"));
    }
    default:
        return QVariant();
    }
}

int ComputerModel::rowCount(const QModelIndex& parent) const
{
    // We should not return a count for valid index values,
    // only the parent (which will not have a "valid" index).
    if (parent.isValid()) {
        return 0;
    }

    return m_Computers.count();
}

QHash<int, QByteArray> ComputerModel::roleNames() const
{
    QHash<int, QByteArray> names;

    names[NameRole] = "name";
    names[OnlineRole] = "online";
    names[PairedRole] = "paired";
    names[BusyRole] = "busy";
    names[WakeableRole] = "wakeable";
    names[StatusUnknownRole] = "statusUnknown";
    names[ServerSupportedRole] = "serverSupported";
    names[DetailsRole] = "details";
    names[UuidRole] = "uuid";
    names[SectionRole] = "section";

    return names;
}

Session* ComputerModel::createSessionForCurrentGame(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    NvComputer* computer = m_Computers[computerIndex];

    // We must currently be streaming a game to use this function
    Q_ASSERT(computer->currentGameId != 0);

    for (NvApp& app : computer->appList) {
        if (app.id == computer->currentGameId) {
            return new Session(computer, app);
        }
    }

    // We have a current running app but it's not in our app list
    Q_ASSERT(false);
    return nullptr;
}

void ComputerModel::deleteComputer(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    beginRemoveRows(QModelIndex(), computerIndex, computerIndex);

    // m_Computer[computerIndex] will be deleted by this call
    m_ComputerManager->deleteHost(m_Computers[computerIndex]);

    // Remove the now invalid item
    m_Computers.removeAt(computerIndex);

    endRemoveRows();
}

class DeferredWakeHostTask : public QRunnable
{
public:
    DeferredWakeHostTask(NvComputer* computer)
        : m_Computer(computer) {}

    void run()
    {
        m_Computer->wake();
    }

private:
    NvComputer* m_Computer;
};

void ComputerModel::wakeComputer(int computerIndex)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    DeferredWakeHostTask* wakeTask = new DeferredWakeHostTask(m_Computers[computerIndex]);
    QThreadPool::globalInstance()->start(wakeTask);
}

void ComputerModel::renameComputer(int computerIndex, QString name)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    m_ComputerManager->renameHost(m_Computers[computerIndex], name);
}

QString ComputerModel::generatePinString()
{
    return m_ComputerManager->generatePinString();
}

class DeferredTestConnectionTask : public QObject, public QRunnable
{
    Q_OBJECT
public:
    void run()
    {
        unsigned int portTestResult = LiTestClientConnectivity("qt.conntest.moonlight-stream.org", 443, ML_PORT_FLAG_ALL);
        if (portTestResult == ML_TEST_RESULT_INCONCLUSIVE) {
            emit connectionTestCompleted(-1, QString());
        }
        else {
            char blockedPorts[512];
            LiStringifyPortFlags(portTestResult, "\n", blockedPorts, sizeof(blockedPorts));
            emit connectionTestCompleted(portTestResult, QString(blockedPorts));
        }
    }

signals:
    void connectionTestCompleted(int result, QString blockedPorts);
};

void ComputerModel::testConnectionForComputer(int)
{
    DeferredTestConnectionTask* testConnectionTask = new DeferredTestConnectionTask();
    QObject::connect(testConnectionTask, &DeferredTestConnectionTask::connectionTestCompleted,
                     this, &ComputerModel::connectionTestCompleted);
    QThreadPool::globalInstance()->start(testConnectionTask);
}

void ComputerModel::pairComputer(int computerIndex, QString pin)
{
    Q_ASSERT(computerIndex < m_Computers.count());

    m_ComputerManager->pairHost(m_Computers[computerIndex], pin);
}

void ComputerModel::handlePairingCompleted(NvComputer*, QString error)
{
    emit pairingCompleted(error.isEmpty() ? QVariant() : error);
}

void ComputerModel::handleComputerStateChanged(NvComputer* computer)
{
    QVector<NvComputer*> newComputerList = m_ComputerManager->getComputers();

    // Check if computers were added or removed
    bool listChanged = (newComputerList.count() != m_Computers.count());
    if (!listChanged) {
        QSet<NvComputer*> oldSet(m_Computers.begin(), m_Computers.end());
        QSet<NvComputer*> newSet(newComputerList.begin(), newComputerList.end());
        listChanged = (oldSet != newSet);
    }

    if (listChanged) {
        // Computers added/removed - full reset required
        beginResetModel();
        m_Computers = newComputerList;
        sortComputers();
        endResetModel();
        return;
    }

    // Same computers. Check if any section assignment changed
    // (which would require re-sorting for proper grouping)
    bool sectionChanged = false;
    for (NvComputer* pc : m_Computers) {
        QReadLocker lock(&pc->lock);
        QString cachedSection = m_CachedSections.value(pc->uuid);
        QString currentSection;
        if (pc->state == NvComputer::CS_ONLINE && pc->pairState == NvComputer::PS_PAIRED)
            currentSection = tr("Online");
        else if (pc->state == NvComputer::CS_ONLINE && pc->pairState != NvComputer::PS_PAIRED)
            currentSection = tr("Not Paired");
        else
            currentSection = tr("Offline");

        if (cachedSection != currentSection) {
            sectionChanged = true;
            break;
        }
    }

    if (sectionChanged) {
        // Section changed - re-sort needed for proper grouping
        beginResetModel();
        sortComputers();
        endResetModel();
    } else {
        // No structural changes - just emit dataChanged for the specific computer
        for (int i = 0; i < m_Computers.count(); i++) {
            if (m_Computers[i] == computer) {
                emit dataChanged(createIndex(i, 0), createIndex(i, 0));
                break;
            }
        }
    }
}

void ComputerModel::sortComputers()
{
    StreamingPreferences* prefs = StreamingPreferences::get();
    bool showSections = prefs->pcShowSections;

    // Snapshot section assignments BEFORE sorting to prevent race conditions.
    // Computer state can change on background threads between comparator calls,
    // making the sort inconsistent if we read state inside the comparator.
    QHash<QString, int> sectionOrder;
    m_CachedSections.clear();
    for (NvComputer* pc : m_Computers) {
        QReadLocker lock(&pc->lock);
        if (pc->state == NvComputer::CS_ONLINE && pc->pairState == NvComputer::PS_PAIRED) {
            sectionOrder[pc->uuid] = 0;
            m_CachedSections[pc->uuid] = tr("Online");
        } else if (pc->state == NvComputer::CS_ONLINE && pc->pairState != NvComputer::PS_PAIRED) {
            sectionOrder[pc->uuid] = 1;
            m_CachedSections[pc->uuid] = tr("Not Paired");
        } else {
            sectionOrder[pc->uuid] = 2;
            m_CachedSections[pc->uuid] = tr("Offline");
        }
    }

    if (m_SortMode == 1 && !m_CustomOrder.isEmpty()) {
        // Custom sort order
        std::sort(m_Computers.begin(), m_Computers.end(),
                  [this, showSections, &sectionOrder](NvComputer* a, NvComputer* b) {
            if (showSections) {
                int secA = sectionOrder.value(a->uuid, 2);
                int secB = sectionOrder.value(b->uuid, 2);
                if (secA != secB) return secA < secB;
            }

            int idxA = m_CustomOrder.indexOf(a->uuid);
            int idxB = m_CustomOrder.indexOf(b->uuid);
            if (idxA < 0 && idxB < 0) {
                QReadLocker lockA(&a->lock);
                QReadLocker lockB(&b->lock);
                return a->name.toLower() < b->name.toLower();
            }
            if (idxA < 0) return false;
            if (idxB < 0) return true;
            return idxA < idxB;
        });
    } else {
        // Alphabetical sort with optional section grouping
        std::sort(m_Computers.begin(), m_Computers.end(),
                  [showSections, &sectionOrder](NvComputer* a, NvComputer* b) {
            if (showSections) {
                int secA = sectionOrder.value(a->uuid, 2);
                int secB = sectionOrder.value(b->uuid, 2);
                if (secA != secB) return secA < secB;
            }

            QReadLocker lockA(&a->lock);
            QReadLocker lockB(&b->lock);
            return a->name.toLower() < b->name.toLower();
        });
    }
}

void ComputerModel::refreshSort()
{
    beginResetModel();
    sortComputers();
    endResetModel();
}

void ComputerModel::setSortMode(int mode)
{
    if (m_SortMode != mode) {
        m_SortMode = mode;

        StreamingPreferences* prefs = StreamingPreferences::get();
        prefs->pcSortMode = static_cast<StreamingPreferences::PcSortMode>(mode);
        prefs->save();

        beginResetModel();
        sortComputers();
        endResetModel();
    }
}

int ComputerModel::getSortMode() const
{
    return m_SortMode;
}

void ComputerModel::moveComputer(int fromIndex, int toIndex)
{
    if (fromIndex < 0 || fromIndex >= m_Computers.count() ||
        toIndex < 0 || toIndex >= m_Computers.count() ||
        fromIndex == toIndex) {
        return;
    }

    // Switch to custom mode if not already
    if (m_SortMode != 1) {
        m_SortMode = 1;
        StreamingPreferences* prefs = StreamingPreferences::get();
        prefs->pcSortMode = StreamingPreferences::PSM_CUSTOM;
        prefs->save();
    }

    int destIndex = toIndex > fromIndex ? toIndex + 1 : toIndex;
    beginMoveRows(QModelIndex(), fromIndex, fromIndex, QModelIndex(), destIndex);
    m_Computers.move(fromIndex, toIndex);
    endMoveRows();

    // Save custom order
    m_CustomOrder.clear();
    for (NvComputer* pc : m_Computers) {
        m_CustomOrder.append(pc->uuid);
    }
    saveCustomOrder();
}

void ComputerModel::saveCustomOrder()
{
    StreamingPreferences* prefs = StreamingPreferences::get();
    prefs->setPcCustomOrder(m_CustomOrder);
}

void ComputerModel::loadCustomOrder()
{
    StreamingPreferences* prefs = StreamingPreferences::get();
    m_CustomOrder = prefs->getPcCustomOrder();
}

#include "computermodel.moc"
