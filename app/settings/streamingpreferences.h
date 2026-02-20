#pragma once

#include <QObject>
#include <QRect>
#include <QVariant>
#include <QQmlEngine>
#include <QStringList>

class QSettings;

class StreamingPreferences : public QObject
{
    Q_OBJECT

public:
    static StreamingPreferences* get(QQmlEngine *qmlEngine = nullptr);

    Q_INVOKABLE static int
    getDefaultBitrate(int width, int height, int fps, bool yuv444);

    Q_INVOKABLE void save();

    Q_INVOKABLE void reload();

    // Per-client settings management
    Q_INVOKABLE void loadForClient(QString clientUuid);
    Q_INVOKABLE void saveForClient(QString clientUuid);
    Q_INVOKABLE void resetClientSettings(QString clientUuid);
    Q_INVOKABLE bool hasClientSettings(QString clientUuid);
    Q_INVOKABLE QString currentClientUuid() const { return m_CurrentClientUuid; }
    Q_INVOKABLE QVariantMap snapshotSettings() const;
    Q_INVOKABLE void restoreSettings(const QVariantMap& settings);

    enum AudioConfig
    {
        AC_STEREO,
        AC_51_SURROUND,
        AC_71_SURROUND
    };
    Q_ENUM(AudioConfig)

    enum VideoCodecConfig
    {
        VCC_AUTO,
        VCC_FORCE_H264,
        VCC_FORCE_HEVC,
        VCC_FORCE_HEVC_HDR_DEPRECATED, // Kept for backwards compatibility
        VCC_FORCE_AV1
    };
    Q_ENUM(VideoCodecConfig)

    enum VideoDecoderSelection
    {
        VDS_AUTO,
        VDS_FORCE_HARDWARE,
        VDS_FORCE_SOFTWARE
    };
    Q_ENUM(VideoDecoderSelection)

    enum WindowMode
    {
        WM_FULLSCREEN,
        WM_FULLSCREEN_DESKTOP,
        WM_WINDOWED
    };
    Q_ENUM(WindowMode)

    enum UIDisplayMode
    {
        UI_WINDOWED,
        UI_MAXIMIZED,
        UI_FULLSCREEN
    };
    Q_ENUM(UIDisplayMode)

    // New entries must go at the end of the enum
    // to avoid renumbering existing entries (which
    // would affect existing user preferences).
    enum Language
    {
        LANG_AUTO,
        LANG_EN,
        LANG_FR,
        LANG_ZH_CN,
        LANG_DE,
        LANG_NB_NO,
        LANG_RU,
        LANG_ES,
        LANG_JA,
        LANG_VI,
        LANG_TH,
        LANG_KO,
        LANG_HU,
        LANG_NL,
        LANG_SV,
        LANG_TR,
        LANG_UK,
        LANG_ZH_TW,
        LANG_PT,
        LANG_PT_BR,
        LANG_EL,
        LANG_IT,
        LANG_HI,
        LANG_PL,
        LANG_CS,
        LANG_HE,
        LANG_CKB,
        LANG_LT,
        LANG_ET,
        LANG_BG,
        LANG_EO,
        LANG_TA,
    };
    Q_ENUM(Language);

    enum CaptureSysKeysMode
    {
        CSK_OFF,
        CSK_FULLSCREEN,
        CSK_ALWAYS,
    };
    Q_ENUM(CaptureSysKeysMode);

    enum AppSortMode
    {
        ASM_ALPHABETICAL,
        ASM_CUSTOM,
    };
    Q_ENUM(AppSortMode);

    enum AppViewMode
    {
        AVM_GRID,
        AVM_LIST,
    };
    Q_ENUM(AppViewMode);

    enum PcSortMode
    {
        PSM_ALPHABETICAL,
        PSM_CUSTOM,
    };
    Q_ENUM(PcSortMode);

    Q_PROPERTY(int width MEMBER width NOTIFY displayModeChanged)
    Q_PROPERTY(int height MEMBER height NOTIFY displayModeChanged)
    Q_PROPERTY(int fps MEMBER fps NOTIFY displayModeChanged)
    Q_PROPERTY(int bitrateKbps MEMBER bitrateKbps NOTIFY bitrateChanged)
    Q_PROPERTY(bool unlockBitrate MEMBER unlockBitrate NOTIFY unlockBitrateChanged)
    Q_PROPERTY(bool autoAdjustBitrate MEMBER autoAdjustBitrate NOTIFY autoAdjustBitrateChanged)
    Q_PROPERTY(bool enableVsync MEMBER enableVsync NOTIFY enableVsyncChanged)
    Q_PROPERTY(bool gameOptimizations MEMBER gameOptimizations NOTIFY gameOptimizationsChanged)
    Q_PROPERTY(bool playAudioOnHost MEMBER playAudioOnHost NOTIFY playAudioOnHostChanged)
    Q_PROPERTY(bool multiController MEMBER multiController NOTIFY multiControllerChanged)
    Q_PROPERTY(bool enableMdns MEMBER enableMdns NOTIFY enableMdnsChanged)
    Q_PROPERTY(bool quitAppAfter MEMBER quitAppAfter NOTIFY quitAppAfterChanged)
    Q_PROPERTY(bool absoluteMouseMode MEMBER absoluteMouseMode NOTIFY absoluteMouseModeChanged)
    Q_PROPERTY(bool absoluteTouchMode MEMBER absoluteTouchMode NOTIFY absoluteTouchModeChanged)
    Q_PROPERTY(bool framePacing MEMBER framePacing NOTIFY framePacingChanged)
    Q_PROPERTY(bool connectionWarnings MEMBER connectionWarnings NOTIFY connectionWarningsChanged)
    Q_PROPERTY(bool configurationWarnings MEMBER configurationWarnings NOTIFY configurationWarningsChanged)
    Q_PROPERTY(bool richPresence MEMBER richPresence NOTIFY richPresenceChanged)
    Q_PROPERTY(bool gamepadMouse MEMBER gamepadMouse NOTIFY gamepadMouseChanged)
    Q_PROPERTY(bool detectNetworkBlocking MEMBER detectNetworkBlocking NOTIFY detectNetworkBlockingChanged)
    Q_PROPERTY(bool showPerformanceOverlay MEMBER showPerformanceOverlay NOTIFY showPerformanceOverlayChanged)
    Q_PROPERTY(AudioConfig audioConfig MEMBER audioConfig NOTIFY audioConfigChanged)
    Q_PROPERTY(VideoCodecConfig videoCodecConfig MEMBER videoCodecConfig NOTIFY videoCodecConfigChanged)
    Q_PROPERTY(bool enableHdr MEMBER enableHdr NOTIFY enableHdrChanged)
    Q_PROPERTY(bool enableYUV444 MEMBER enableYUV444 NOTIFY enableYUV444Changed)
    Q_PROPERTY(VideoDecoderSelection videoDecoderSelection MEMBER videoDecoderSelection NOTIFY videoDecoderSelectionChanged)
    Q_PROPERTY(WindowMode windowMode MEMBER windowMode NOTIFY windowModeChanged)
    Q_PROPERTY(WindowMode recommendedFullScreenMode MEMBER recommendedFullScreenMode CONSTANT)
    Q_PROPERTY(UIDisplayMode uiDisplayMode MEMBER uiDisplayMode NOTIFY uiDisplayModeChanged)
    Q_PROPERTY(bool swapMouseButtons MEMBER swapMouseButtons NOTIFY mouseButtonsChanged)
    Q_PROPERTY(bool muteOnFocusLoss MEMBER muteOnFocusLoss NOTIFY muteOnFocusLossChanged)
    Q_PROPERTY(bool backgroundGamepad MEMBER backgroundGamepad NOTIFY backgroundGamepadChanged)
    Q_PROPERTY(bool reverseScrollDirection MEMBER reverseScrollDirection NOTIFY reverseScrollDirectionChanged)
    Q_PROPERTY(bool swapFaceButtons MEMBER swapFaceButtons NOTIFY swapFaceButtonsChanged)
    Q_PROPERTY(bool keepAwake MEMBER keepAwake NOTIFY keepAwakeChanged)
    Q_PROPERTY(CaptureSysKeysMode captureSysKeysMode MEMBER captureSysKeysMode NOTIFY captureSysKeysModeChanged)
    Q_PROPERTY(Language language MEMBER language NOTIFY languageChanged);
    Q_PROPERTY(AppSortMode appSortMode MEMBER appSortMode NOTIFY appSortModeChanged)
    Q_PROPERTY(AppViewMode appViewMode MEMBER appViewMode NOTIFY appViewModeChanged)
    Q_PROPERTY(int appTileScale MEMBER appTileScale NOTIFY appTileScaleChanged)
    Q_PROPERTY(PcSortMode pcSortMode MEMBER pcSortMode NOTIFY pcSortModeChanged)
    Q_PROPERTY(int pcTileScale MEMBER pcTileScale NOTIFY pcTileScaleChanged)
    Q_PROPERTY(bool pcShowSections MEMBER pcShowSections NOTIFY pcShowSectionsChanged)
    Q_PROPERTY(bool showPcInfo MEMBER showPcInfo NOTIFY showPcInfoChanged)

    Q_INVOKABLE bool retranslate();

    // Custom shortcut management
    Q_INVOKABLE QVariantList getCustomShortcuts() const;
    Q_INVOKABLE void setCustomShortcut(const QString& action, const QString& shortcut);
    Q_INVOKABLE void removeCustomShortcut(const QString& action);
    Q_INVOKABLE QString getShortcutForAction(const QString& action) const;
    Q_INVOKABLE QStringList getAvailableShortcutActions() const;

    // Custom order management for apps (per computer)
    Q_INVOKABLE QStringList getAppCustomOrder(const QString& computerUuid) const;
    Q_INVOKABLE void setAppCustomOrder(const QString& computerUuid, const QStringList& appIds);

    // Custom order management for PCs
    Q_INVOKABLE QStringList getPcCustomOrder() const;
    Q_INVOKABLE void setPcCustomOrder(const QStringList& pcUuids);

    // Folder management for apps (per computer)
    Q_INVOKABLE QStringList getAppFolders(const QString& computerUuid) const;
    Q_INVOKABLE void createAppFolder(const QString& computerUuid, const QString& folderName);
    Q_INVOKABLE void deleteAppFolder(const QString& computerUuid, const QString& folderName);
    Q_INVOKABLE void renameAppFolder(const QString& computerUuid, const QString& oldName, const QString& newName);
    Q_INVOKABLE QStringList getAppsInFolder(const QString& computerUuid, const QString& folderName) const;
    Q_INVOKABLE void setAppsInFolder(const QString& computerUuid, const QString& folderName, const QStringList& appIds);
    Q_INVOKABLE void addAppToFolder(const QString& computerUuid, const QString& folderName, const QString& appId);
    Q_INVOKABLE void removeAppFromFolder(const QString& computerUuid, const QString& folderName, const QString& appId);
    Q_INVOKABLE QString getAppFolder(const QString& computerUuid, const QString& appId) const;

    // Directly accessible members for preferences
    int width;
    int height;
    int fps;
    int bitrateKbps;
    bool unlockBitrate;
    bool autoAdjustBitrate;
    bool enableVsync;
    bool gameOptimizations;
    bool playAudioOnHost;
    bool multiController;
    bool enableMdns;
    bool quitAppAfter;
    bool absoluteMouseMode;
    bool absoluteTouchMode;
    bool framePacing;
    bool connectionWarnings;
    bool configurationWarnings;
    bool richPresence;
    bool gamepadMouse;
    bool detectNetworkBlocking;
    bool showPerformanceOverlay;
    bool swapMouseButtons;
    bool muteOnFocusLoss;
    bool backgroundGamepad;
    bool reverseScrollDirection;
    bool swapFaceButtons;
    bool keepAwake;
    int packetSize;
    AudioConfig audioConfig;
    VideoCodecConfig videoCodecConfig;
    bool enableHdr;
    bool enableYUV444;
    VideoDecoderSelection videoDecoderSelection;
    WindowMode windowMode;
    WindowMode recommendedFullScreenMode;
    UIDisplayMode uiDisplayMode;
    Language language;
    CaptureSysKeysMode captureSysKeysMode;
    AppSortMode appSortMode;
    AppViewMode appViewMode;
    int appTileScale;
    PcSortMode pcSortMode;
    int pcTileScale;
    bool pcShowSections;
    bool showPcInfo;

signals:
    void displayModeChanged();
    void bitrateChanged();
    void unlockBitrateChanged();
    void autoAdjustBitrateChanged();
    void enableVsyncChanged();
    void gameOptimizationsChanged();
    void playAudioOnHostChanged();
    void multiControllerChanged();
    void unsupportedFpsChanged();
    void enableMdnsChanged();
    void quitAppAfterChanged();
    void absoluteMouseModeChanged();
    void absoluteTouchModeChanged();
    void audioConfigChanged();
    void videoCodecConfigChanged();
    void enableHdrChanged();
    void enableYUV444Changed();
    void videoDecoderSelectionChanged();
    void uiDisplayModeChanged();
    void windowModeChanged();
    void framePacingChanged();
    void connectionWarningsChanged();
    void configurationWarningsChanged();
    void richPresenceChanged();
    void gamepadMouseChanged();
    void detectNetworkBlockingChanged();
    void showPerformanceOverlayChanged();
    void mouseButtonsChanged();
    void muteOnFocusLossChanged();
    void backgroundGamepadChanged();
    void reverseScrollDirectionChanged();
    void swapFaceButtonsChanged();
    void captureSysKeysModeChanged();
    void keepAwakeChanged();
    void languageChanged();
    void appSortModeChanged();
    void appViewModeChanged();
    void appTileScaleChanged();
    void pcSortModeChanged();
    void pcTileScaleChanged();
    void pcShowSectionsChanged();
    void showPcInfoChanged();

private:
    explicit StreamingPreferences(QQmlEngine *qmlEngine);

    QString getSuffixFromLanguage(Language lang);
    void emitAllChanged();

    QQmlEngine* m_QmlEngine;
    QString m_CurrentClientUuid;
};

