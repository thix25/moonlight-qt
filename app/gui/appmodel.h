#pragma once

#include "backend/boxartmanager.h"
#include "backend/computermanager.h"
#include "streaming/session.h"

#include <QAbstractListModel>

class AppModel : public QAbstractListModel
{
    Q_OBJECT

    enum Roles
    {
        NameRole = Qt::UserRole,
        RunningRole,
        BoxArtRole,
        HiddenRole,
        AppIdRole,
        DirectLaunchRole,
        AppCollectorGameRole,
        FolderRole,
    };

public:
    explicit AppModel(QObject *parent = nullptr);

    // Must be called before any QAbstractListModel functions
    Q_INVOKABLE void initialize(ComputerManager* computerManager, int computerIndex, bool showHiddenGames);

    Q_INVOKABLE Session* createSessionForApp(int appIndex);

    Q_INVOKABLE int getDirectLaunchAppIndex();

    Q_INVOKABLE int getRunningAppId();

    Q_INVOKABLE QString getRunningAppName();

    Q_INVOKABLE QString getAppName(int appIndex) const;
    Q_INVOKABLE int getAppId(int appIndex) const;

    Q_INVOKABLE void quitRunningApp();

    Q_INVOKABLE void setAppHidden(int appIndex, bool hidden);

    Q_INVOKABLE void setAppDirectLaunch(int appIndex, bool directLaunch);

    // Sort mode management
    Q_INVOKABLE void setSortMode(int mode);
    Q_INVOKABLE int getSortMode() const;

    // Custom ordering: move an app from one index to another
    Q_INVOKABLE void moveApp(int fromIndex, int toIndex);

    // Get the computer UUID for custom order persistence
    Q_INVOKABLE QString getComputerUuid() const;

    // Folder support
    Q_INVOKABLE void setCurrentFolder(const QString& folderName);
    Q_INVOKABLE QString getCurrentFolder() const;

    // Convenience for QML
    Q_INVOKABLE int count() const { return m_VisibleApps.count(); }

    QVariant data(const QModelIndex &index, int role) const override;

    int rowCount(const QModelIndex &parent) const override;

    virtual QHash<int, QByteArray> roleNames() const override;

private slots:
    void handleComputerStateChanged(NvComputer* computer);

    void handleBoxArtLoaded(NvComputer* computer, NvApp app, QUrl image);

signals:
    void computerLost();

private:
    void updateAppList(QVector<NvApp> newList);

    QVector<NvApp> getVisibleApps(const QVector<NvApp>& appList);

    bool isAppCurrentlyVisible(const NvApp& app);

    void sortVisibleApps();

    void saveCustomOrder();
    void loadCustomOrder();

    NvComputer* m_Computer;
    BoxArtManager m_BoxArtManager;
    ComputerManager* m_ComputerManager;
    QVector<NvApp> m_VisibleApps, m_AllApps;
    int m_CurrentGameId;
    bool m_ShowHiddenGames;
    int m_SortMode; // 0=alphabetical, 1=custom
    QStringList m_CustomOrder; // app IDs in custom order
    QString m_CurrentFolder; // empty = root, otherwise folder name
};
