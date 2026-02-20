#include "streamingpreferences.h"
#include "utils.h"

#include <QSettings>
#include <QTranslator>
#include <QCoreApplication>
#include <QLocale>
#include <QReadWriteLock>
#include <QtMath>

#include <QtDebug>

#define SER_STREAMSETTINGS "streamsettings"
#define SER_WIDTH "width"
#define SER_HEIGHT "height"
#define SER_FPS "fps"
#define SER_BITRATE "bitrate"
#define SER_UNLOCK_BITRATE "unlockbitrate"
#define SER_AUTOADJUSTBITRATE "autoadjustbitrate"
#define SER_FULLSCREEN "fullscreen"
#define SER_VSYNC "vsync"
#define SER_GAMEOPTS "gameopts"
#define SER_HOSTAUDIO "hostaudio"
#define SER_MULTICONT "multicontroller"
#define SER_AUDIOCFG "audiocfg"
#define SER_VIDEOCFG "videocfg"
#define SER_HDR "hdr"
#define SER_YUV444 "yuv444"
#define SER_VIDEODEC "videodec"
#define SER_WINDOWMODE "windowmode"
#define SER_MDNS "mdns"
#define SER_QUITAPPAFTER "quitAppAfter"
#define SER_ABSMOUSEMODE "mouseacceleration"
#define SER_ABSTOUCHMODE "abstouchmode"
#define SER_STARTWINDOWED "startwindowed"
#define SER_FRAMEPACING "framepacing"
#define SER_CONNWARNINGS "connwarnings"
#define SER_CONFWARNINGS "confwarnings"
#define SER_UIDISPLAYMODE "uidisplaymode"
#define SER_RICHPRESENCE "richpresence"
#define SER_GAMEPADMOUSE "gamepadmouse"
#define SER_DEFAULTVER "defaultver"
#define SER_PACKETSIZE "packetsize"
#define SER_DETECTNETBLOCKING "detectnetblocking"
#define SER_SHOWPERFOVERLAY "showperfoverlay"
#define SER_SWAPMOUSEBUTTONS "swapmousebuttons"
#define SER_MUTEONFOCUSLOSS "muteonfocusloss"
#define SER_BACKGROUNDGAMEPAD "backgroundgamepad"
#define SER_REVERSESCROLL "reversescroll"
#define SER_SWAPFACEBUTTONS "swapfacebuttons"
#define SER_CAPTURESYSKEYS "capturesyskeys"
#define SER_KEEPAWAKE "keepawake"
#define SER_LANGUAGE "language"
#define SER_APPSORTMODE "appsortmode"
#define SER_APPVIEWMODE "appviewmode"
#define SER_APPTILESCALE "apptilescale"
#define SER_PCSORTMODE "pcsortmode"
#define SER_PCTILESCALE "pctilescale"
#define SER_PCSHOWSECTIONS "pcshowsections"
#define SER_SHOWPCINFO "showpcinfo"

#define CURRENT_DEFAULT_VER 2

static StreamingPreferences* s_GlobalPrefs;
static QReadWriteLock s_GlobalPrefsLock;

StreamingPreferences::StreamingPreferences(QQmlEngine *qmlEngine)
    : m_QmlEngine(qmlEngine)
{
    reload();
}

StreamingPreferences* StreamingPreferences::get(QQmlEngine *qmlEngine)
{
    {
        QReadLocker readGuard(&s_GlobalPrefsLock);

        // If we have a preference object and it's associated with a QML engine or
        // if the caller didn't specify a QML engine, return the existing object.
        if (s_GlobalPrefs && (s_GlobalPrefs->m_QmlEngine || !qmlEngine)) {
            // The lifetime logic here relies on the QML engine also being a singleton.
            Q_ASSERT(!qmlEngine || s_GlobalPrefs->m_QmlEngine == qmlEngine);
            return s_GlobalPrefs;
        }
    }

    {
        QWriteLocker writeGuard(&s_GlobalPrefsLock);

        // If we already have an preference object but the QML engine is now available,
        // associate the QML engine with the preferences.
        if (s_GlobalPrefs) {
            if (!s_GlobalPrefs->m_QmlEngine) {
                s_GlobalPrefs->m_QmlEngine = qmlEngine;
            }
            else {
                // We could reach this codepath if another thread raced with us
                // and created the object while we were outside the pref lock.
                Q_ASSERT(!qmlEngine || s_GlobalPrefs->m_QmlEngine == qmlEngine);
            }
        }
        else {
            s_GlobalPrefs = new StreamingPreferences(qmlEngine);
        }

        return s_GlobalPrefs;
    }
}

void StreamingPreferences::reload()
{
    QSettings settings;

    // Clear client UUID when reloading global settings
    m_CurrentClientUuid.clear();

    int defaultVer = settings.value(SER_DEFAULTVER, 0).toInt();

#ifdef Q_OS_DARWIN
    recommendedFullScreenMode = WindowMode::WM_FULLSCREEN_DESKTOP;
#else
    // Wayland doesn't support modesetting, so use fullscreen desktop mode.
    if (WMUtils::isRunningWayland()) {
        recommendedFullScreenMode = WindowMode::WM_FULLSCREEN_DESKTOP;
    }
    else {
        recommendedFullScreenMode = WindowMode::WM_FULLSCREEN;
    }
#endif

    width = settings.value(SER_WIDTH, 1280).toInt();
    height = settings.value(SER_HEIGHT, 720).toInt();
    fps = settings.value(SER_FPS, 60).toInt();
    enableYUV444 = settings.value(SER_YUV444, false).toBool();
    bitrateKbps = settings.value(SER_BITRATE, getDefaultBitrate(width, height, fps, enableYUV444)).toInt();
    unlockBitrate = settings.value(SER_UNLOCK_BITRATE, false).toBool();
    autoAdjustBitrate = settings.value(SER_AUTOADJUSTBITRATE, true).toBool();
    enableVsync = settings.value(SER_VSYNC, true).toBool();
    gameOptimizations = settings.value(SER_GAMEOPTS, true).toBool();
    playAudioOnHost = settings.value(SER_HOSTAUDIO, false).toBool();
    multiController = settings.value(SER_MULTICONT, true).toBool();
    enableMdns = settings.value(SER_MDNS, true).toBool();
    quitAppAfter = settings.value(SER_QUITAPPAFTER, false).toBool();
    absoluteMouseMode = settings.value(SER_ABSMOUSEMODE, false).toBool();
    absoluteTouchMode = settings.value(SER_ABSTOUCHMODE, true).toBool();
    framePacing = settings.value(SER_FRAMEPACING, false).toBool();
    connectionWarnings = settings.value(SER_CONNWARNINGS, true).toBool();
    configurationWarnings = settings.value(SER_CONFWARNINGS, true).toBool();
    richPresence = settings.value(SER_RICHPRESENCE, true).toBool();
    gamepadMouse = settings.value(SER_GAMEPADMOUSE, true).toBool();
    detectNetworkBlocking = settings.value(SER_DETECTNETBLOCKING, true).toBool();
    showPerformanceOverlay = settings.value(SER_SHOWPERFOVERLAY, false).toBool();
    packetSize = settings.value(SER_PACKETSIZE, 0).toInt();
    swapMouseButtons = settings.value(SER_SWAPMOUSEBUTTONS, false).toBool();
    muteOnFocusLoss = settings.value(SER_MUTEONFOCUSLOSS, false).toBool();
    backgroundGamepad = settings.value(SER_BACKGROUNDGAMEPAD, false).toBool();
    reverseScrollDirection = settings.value(SER_REVERSESCROLL, false).toBool();
    swapFaceButtons = settings.value(SER_SWAPFACEBUTTONS, false).toBool();
    keepAwake = settings.value(SER_KEEPAWAKE, true).toBool();
    enableHdr = settings.value(SER_HDR, false).toBool();
    captureSysKeysMode = static_cast<CaptureSysKeysMode>(settings.value(SER_CAPTURESYSKEYS,
                                                         static_cast<int>(CaptureSysKeysMode::CSK_OFF)).toInt());
    audioConfig = static_cast<AudioConfig>(settings.value(SER_AUDIOCFG,
                                                  static_cast<int>(AudioConfig::AC_STEREO)).toInt());
    videoCodecConfig = static_cast<VideoCodecConfig>(settings.value(SER_VIDEOCFG,
                                                  static_cast<int>(VideoCodecConfig::VCC_AUTO)).toInt());
    videoDecoderSelection = static_cast<VideoDecoderSelection>(settings.value(SER_VIDEODEC,
                                                  static_cast<int>(VideoDecoderSelection::VDS_AUTO)).toInt());
    windowMode = static_cast<WindowMode>(settings.value(SER_WINDOWMODE,
                                                        // Try to load from the old preference value too
                                                        static_cast<int>(settings.value(SER_FULLSCREEN, true).toBool() ?
                                                                             recommendedFullScreenMode : WindowMode::WM_WINDOWED)).toInt());
    uiDisplayMode = static_cast<UIDisplayMode>(settings.value(SER_UIDISPLAYMODE,
                                               static_cast<int>(settings.value(SER_STARTWINDOWED, true).toBool() ? UIDisplayMode::UI_WINDOWED
                                                                                                                 : UIDisplayMode::UI_MAXIMIZED)).toInt());
    language = static_cast<Language>(settings.value(SER_LANGUAGE,
                                                    static_cast<int>(Language::LANG_AUTO)).toInt());
    appSortMode = static_cast<AppSortMode>(settings.value(SER_APPSORTMODE,
                                                          static_cast<int>(AppSortMode::ASM_ALPHABETICAL)).toInt());
    appViewMode = static_cast<AppViewMode>(settings.value(SER_APPVIEWMODE,
                                                          static_cast<int>(AppViewMode::AVM_GRID)).toInt());
    appTileScale = settings.value(SER_APPTILESCALE, 100).toInt();
    pcSortMode = static_cast<PcSortMode>(settings.value(SER_PCSORTMODE,
                                                        static_cast<int>(PcSortMode::PSM_ALPHABETICAL)).toInt());
    pcTileScale = settings.value(SER_PCTILESCALE, 100).toInt();
    pcShowSections = settings.value(SER_PCSHOWSECTIONS, true).toBool();
    showPcInfo = settings.value(SER_SHOWPCINFO, false).toBool();


    // Perform default settings updates as required based on last default version
    if (defaultVer < 1) {
#ifdef Q_OS_DARWIN
        // Update window mode setting on macOS from full-screen (old default) to borderless windowed (new default)
        if (windowMode == WindowMode::WM_FULLSCREEN) {
            windowMode = WindowMode::WM_FULLSCREEN_DESKTOP;
        }
#endif
    }
    if (defaultVer < 2) {
        if (windowMode == WindowMode::WM_FULLSCREEN && WMUtils::isRunningWayland()) {
            windowMode = WindowMode::WM_FULLSCREEN_DESKTOP;
        }
    }

    // Fixup VCC value to the new settings format with codec and HDR separate
    if (videoCodecConfig == VCC_FORCE_HEVC_HDR_DEPRECATED) {
        videoCodecConfig = VCC_AUTO;
        enableHdr = true;
    }

    emitAllChanged();
}

bool StreamingPreferences::retranslate()
{
    static QTranslator* translator = nullptr;

#if QT_VERSION < QT_VERSION_CHECK(5, 10, 0)
    if (m_QmlEngine != nullptr) {
        // Dynamic retranslation is not supported until Qt 5.10
        return false;
    }
#endif

    QTranslator* newTranslator = new QTranslator();
    QString languageSuffix = getSuffixFromLanguage(language);

    // Remove the old translator, even if we can't load a new one.
    // Otherwise we'll be stuck with the old translated values instead
    // of defaulting to English.
    if (translator != nullptr) {
        QCoreApplication::removeTranslator(translator);
        delete translator;
        translator = nullptr;
    }

    if (newTranslator->load(QString(":/languages/qml_") + languageSuffix)) {
        qInfo() << "Successfully loaded translation for" << languageSuffix;

        translator = newTranslator;
        QCoreApplication::installTranslator(translator);
    }
    else {
        qInfo() << "No translation available for" << languageSuffix;
        delete newTranslator;
    }

    if (m_QmlEngine != nullptr) {
#if QT_VERSION >= QT_VERSION_CHECK(5, 10, 0)
        // This is a dynamic retranslation from the settings page.
        // We have to kick the QML engine into reloading our text.
        m_QmlEngine->retranslate();
#else
        // Unreachable below Qt 5.10 due to the check above
        Q_ASSERT(false);
#endif
    }
    else {
        // This is a translation from a non-QML context, which means
        // it is probably app startup. There's nothing to refresh.
    }

    return true;
}

QString StreamingPreferences::getSuffixFromLanguage(StreamingPreferences::Language lang)
{
    switch (lang)
    {
    case LANG_DE:
        return "de";
    case LANG_EN:
        return "en";
    case LANG_FR:
        return "fr";
    case LANG_ZH_CN:
        return "zh_CN";
    case LANG_NB_NO:
        return "nb_NO";
    case LANG_RU:
        return "ru";
    case LANG_ES:
        return "es";
    case LANG_JA:
        return "ja";
    case LANG_VI:
        return "vi";
    case LANG_TH:
        return "th";
    case LANG_KO:
        return "ko";
    case LANG_HU:
        return "hu";
    case LANG_NL:
        return "nl";
    case LANG_SV:
        return "sv";
    case LANG_TR:
        return "tr";
    case LANG_UK:
        return "uk";
    case LANG_ZH_TW:
        return "zh_TW";
    case LANG_PT:
        return "pt";
    case LANG_PT_BR:
        return "pt_BR";
    case LANG_EL:
        return "el";
    case LANG_IT:
        return "it";
    case LANG_HI:
        return "hi";
    case LANG_PL:
        return "pl";
    case LANG_CS:
        return "cs";
    case LANG_HE:
        return "he";
    case LANG_CKB:
        return "ckb";
    case LANG_LT:
        return "lt";
    case LANG_ET:
        return "et";
    case LANG_BG:
        return "bg";
    case LANG_EO:
        return "eo";
    case LANG_TA:
        return "ta";
    case LANG_AUTO:
    default:
        return QLocale::system().name();
    }
}

void StreamingPreferences::save()
{
    // Don't save to global settings if we're currently viewing client-specific settings
    if (!m_CurrentClientUuid.isEmpty()) {
        qWarning() << "Attempted to save global settings while client settings are loaded for" << m_CurrentClientUuid;
        return;
    }

    QSettings settings;

    settings.setValue(SER_WIDTH, width);
    settings.setValue(SER_HEIGHT, height);
    settings.setValue(SER_FPS, fps);
    settings.setValue(SER_BITRATE, bitrateKbps);
    settings.setValue(SER_UNLOCK_BITRATE, unlockBitrate);
    settings.setValue(SER_AUTOADJUSTBITRATE, autoAdjustBitrate);
    settings.setValue(SER_VSYNC, enableVsync);
    settings.setValue(SER_GAMEOPTS, gameOptimizations);
    settings.setValue(SER_HOSTAUDIO, playAudioOnHost);
    settings.setValue(SER_MULTICONT, multiController);
    settings.setValue(SER_MDNS, enableMdns);
    settings.setValue(SER_QUITAPPAFTER, quitAppAfter);
    settings.setValue(SER_ABSMOUSEMODE, absoluteMouseMode);
    settings.setValue(SER_ABSTOUCHMODE, absoluteTouchMode);
    settings.setValue(SER_FRAMEPACING, framePacing);
    settings.setValue(SER_CONNWARNINGS, connectionWarnings);
    settings.setValue(SER_CONFWARNINGS, configurationWarnings);
    settings.setValue(SER_RICHPRESENCE, richPresence);
    settings.setValue(SER_GAMEPADMOUSE, gamepadMouse);
    settings.setValue(SER_PACKETSIZE, packetSize);
    settings.setValue(SER_DETECTNETBLOCKING, detectNetworkBlocking);
    settings.setValue(SER_SHOWPERFOVERLAY, showPerformanceOverlay);
    settings.setValue(SER_AUDIOCFG, static_cast<int>(audioConfig));
    settings.setValue(SER_HDR, enableHdr);
    settings.setValue(SER_YUV444, enableYUV444);
    settings.setValue(SER_VIDEOCFG, static_cast<int>(videoCodecConfig));
    settings.setValue(SER_VIDEODEC, static_cast<int>(videoDecoderSelection));
    settings.setValue(SER_WINDOWMODE, static_cast<int>(windowMode));
    settings.setValue(SER_UIDISPLAYMODE, static_cast<int>(uiDisplayMode));
    settings.setValue(SER_LANGUAGE, static_cast<int>(language));
    settings.setValue(SER_DEFAULTVER, CURRENT_DEFAULT_VER);
    settings.setValue(SER_SWAPMOUSEBUTTONS, swapMouseButtons);
    settings.setValue(SER_MUTEONFOCUSLOSS, muteOnFocusLoss);
    settings.setValue(SER_BACKGROUNDGAMEPAD, backgroundGamepad);
    settings.setValue(SER_REVERSESCROLL, reverseScrollDirection);
    settings.setValue(SER_SWAPFACEBUTTONS, swapFaceButtons);
    settings.setValue(SER_CAPTURESYSKEYS, captureSysKeysMode);
    settings.setValue(SER_KEEPAWAKE, keepAwake);
    settings.setValue(SER_APPSORTMODE, static_cast<int>(appSortMode));
    settings.setValue(SER_APPVIEWMODE, static_cast<int>(appViewMode));
    settings.setValue(SER_APPTILESCALE, appTileScale);
    settings.setValue(SER_PCSORTMODE, static_cast<int>(pcSortMode));
    settings.setValue(SER_PCTILESCALE, pcTileScale);
    settings.setValue(SER_PCSHOWSECTIONS, pcShowSections);
    settings.setValue(SER_SHOWPCINFO, showPcInfo);
}

int StreamingPreferences::getDefaultBitrate(int width, int height, int fps, bool yuv444)
{
    // Don't scale bitrate linearly beyond 60 FPS. It's definitely not a linear
    // bitrate increase for frame rate once we get to values that high.
    float frameRateFactor = (fps <= 60 ? fps : (qSqrt(fps / 60.f) * 60.f)) / 30.f;

    // TODO: Collect some empirical data to see if these defaults make sense.
    // We're just using the values that the Shield used, as we have for years.
    static const struct resTable {
        int pixels;
        int factor;
    } resTable[] {
        { 640 * 360, 1 },
        { 854 * 480, 2 },
        { 1280 * 720, 5 },
        { 1920 * 1080, 10 },
        { 2560 * 1440, 20 },
        { 3840 * 2160, 40 },
        { -1, -1 },
    };

    // Calculate the resolution factor by linear interpolation of the resolution table
    float resolutionFactor;
    int pixels = width * height;
    for (int i = 0;; i++) {
        if (pixels == resTable[i].pixels) {
            // We can bail immediately for exact matches
            resolutionFactor = resTable[i].factor;
            break;
        }
        else if (pixels < resTable[i].pixels) {
            if (i == 0) {
                // Never go below the lowest resolution entry
                resolutionFactor = resTable[i].factor;
            }
            else {
                // Interpolate between the entry greater than the chosen resolution (i) and the entry less than the chosen resolution (i-1)
                resolutionFactor = ((float)(pixels - resTable[i-1].pixels) / (resTable[i].pixels - resTable[i-1].pixels)) * (resTable[i].factor - resTable[i-1].factor) + resTable[i-1].factor;
            }
            break;
        }
        else if (resTable[i].pixels == -1) {
            // Never go above the highest resolution entry
            resolutionFactor = resTable[i-1].factor;
            break;
        }
    }

    if (yuv444) {
        // This is rough estimation based on the fact that 4:4:4 doubles the amount of raw YUV data compared to 4:2:0
        resolutionFactor *= 2;
    }

    return qRound(resolutionFactor * frameRateFactor) * 1000;
}

void StreamingPreferences::loadForClient(QString clientUuid)
{
    if (clientUuid.isEmpty()) {
        qWarning() << "Attempted to load settings for empty client UUID";
        return;
    }

    QSettings settings;
    settings.beginGroup("clients/" + clientUuid);

    m_CurrentClientUuid = clientUuid;

    // Load client-specific settings with fallback to current (global) values
    width = settings.value(SER_WIDTH, width).toInt();
    height = settings.value(SER_HEIGHT, height).toInt();
    fps = settings.value(SER_FPS, fps).toInt();
    enableYUV444 = settings.value(SER_YUV444, enableYUV444).toBool();
    bitrateKbps = settings.value(SER_BITRATE, bitrateKbps).toInt();
    unlockBitrate = settings.value(SER_UNLOCK_BITRATE, unlockBitrate).toBool();
    autoAdjustBitrate = settings.value(SER_AUTOADJUSTBITRATE, autoAdjustBitrate).toBool();
    enableVsync = settings.value(SER_VSYNC, enableVsync).toBool();
    gameOptimizations = settings.value(SER_GAMEOPTS, gameOptimizations).toBool();
    playAudioOnHost = settings.value(SER_HOSTAUDIO, playAudioOnHost).toBool();
    multiController = settings.value(SER_MULTICONT, multiController).toBool();
    enableMdns = settings.value(SER_MDNS, enableMdns).toBool();
    quitAppAfter = settings.value(SER_QUITAPPAFTER, quitAppAfter).toBool();
    absoluteMouseMode = settings.value(SER_ABSMOUSEMODE, absoluteMouseMode).toBool();
    absoluteTouchMode = settings.value(SER_ABSTOUCHMODE, absoluteTouchMode).toBool();
    framePacing = settings.value(SER_FRAMEPACING, framePacing).toBool();
    connectionWarnings = settings.value(SER_CONNWARNINGS, connectionWarnings).toBool();
    configurationWarnings = settings.value(SER_CONFWARNINGS, configurationWarnings).toBool();
    richPresence = settings.value(SER_RICHPRESENCE, richPresence).toBool();
    gamepadMouse = settings.value(SER_GAMEPADMOUSE, gamepadMouse).toBool();
    detectNetworkBlocking = settings.value(SER_DETECTNETBLOCKING, detectNetworkBlocking).toBool();
    showPerformanceOverlay = settings.value(SER_SHOWPERFOVERLAY, showPerformanceOverlay).toBool();
    packetSize = settings.value(SER_PACKETSIZE, packetSize).toInt();
    swapMouseButtons = settings.value(SER_SWAPMOUSEBUTTONS, swapMouseButtons).toBool();
    muteOnFocusLoss = settings.value(SER_MUTEONFOCUSLOSS, muteOnFocusLoss).toBool();
    backgroundGamepad = settings.value(SER_BACKGROUNDGAMEPAD, backgroundGamepad).toBool();
    reverseScrollDirection = settings.value(SER_REVERSESCROLL, reverseScrollDirection).toBool();
    swapFaceButtons = settings.value(SER_SWAPFACEBUTTONS, swapFaceButtons).toBool();
    keepAwake = settings.value(SER_KEEPAWAKE, keepAwake).toBool();
    enableHdr = settings.value(SER_HDR, enableHdr).toBool();
    captureSysKeysMode = static_cast<CaptureSysKeysMode>(settings.value(SER_CAPTURESYSKEYS,
                                                         static_cast<int>(captureSysKeysMode)).toInt());
    audioConfig = static_cast<AudioConfig>(settings.value(SER_AUDIOCFG,
                                                  static_cast<int>(audioConfig)).toInt());
    videoCodecConfig = static_cast<VideoCodecConfig>(settings.value(SER_VIDEOCFG,
                                                  static_cast<int>(videoCodecConfig)).toInt());
    videoDecoderSelection = static_cast<VideoDecoderSelection>(settings.value(SER_VIDEODEC,
                                                  static_cast<int>(videoDecoderSelection)).toInt());
    windowMode = static_cast<WindowMode>(settings.value(SER_WINDOWMODE,
                                                        static_cast<int>(windowMode)).toInt());

    settings.endGroup();

    emitAllChanged();

    qInfo() << "Loaded client-specific settings for UUID:" << clientUuid
            << "resolution:" << width << "x" << height
            << "fps:" << fps
            << "codec:" << static_cast<int>(videoCodecConfig)
            << "bitrate:" << bitrateKbps << "kbps";
}

void StreamingPreferences::saveForClient(QString clientUuid)
{
    if (clientUuid.isEmpty()) {
        qWarning() << "Attempted to save settings for empty client UUID";
        return;
    }

    QSettings settings;
    settings.beginGroup("clients/" + clientUuid);

    settings.setValue(SER_WIDTH, width);
    settings.setValue(SER_HEIGHT, height);
    settings.setValue(SER_FPS, fps);
    settings.setValue(SER_BITRATE, bitrateKbps);
    settings.setValue(SER_UNLOCK_BITRATE, unlockBitrate);
    settings.setValue(SER_AUTOADJUSTBITRATE, autoAdjustBitrate);
    settings.setValue(SER_VSYNC, enableVsync);
    settings.setValue(SER_GAMEOPTS, gameOptimizations);
    settings.setValue(SER_HOSTAUDIO, playAudioOnHost);
    settings.setValue(SER_MULTICONT, multiController);
    settings.setValue(SER_MDNS, enableMdns);
    settings.setValue(SER_QUITAPPAFTER, quitAppAfter);
    settings.setValue(SER_ABSMOUSEMODE, absoluteMouseMode);
    settings.setValue(SER_ABSTOUCHMODE, absoluteTouchMode);
    settings.setValue(SER_FRAMEPACING, framePacing);
    settings.setValue(SER_CONNWARNINGS, connectionWarnings);
    settings.setValue(SER_CONFWARNINGS, configurationWarnings);
    settings.setValue(SER_RICHPRESENCE, richPresence);
    settings.setValue(SER_GAMEPADMOUSE, gamepadMouse);
    settings.setValue(SER_PACKETSIZE, packetSize);
    settings.setValue(SER_DETECTNETBLOCKING, detectNetworkBlocking);
    settings.setValue(SER_SHOWPERFOVERLAY, showPerformanceOverlay);
    settings.setValue(SER_AUDIOCFG, static_cast<int>(audioConfig));
    settings.setValue(SER_HDR, enableHdr);
    settings.setValue(SER_YUV444, enableYUV444);
    settings.setValue(SER_VIDEOCFG, static_cast<int>(videoCodecConfig));
    settings.setValue(SER_VIDEODEC, static_cast<int>(videoDecoderSelection));
    settings.setValue(SER_WINDOWMODE, static_cast<int>(windowMode));
    settings.setValue(SER_SWAPMOUSEBUTTONS, swapMouseButtons);
    settings.setValue(SER_MUTEONFOCUSLOSS, muteOnFocusLoss);
    settings.setValue(SER_BACKGROUNDGAMEPAD, backgroundGamepad);
    settings.setValue(SER_REVERSESCROLL, reverseScrollDirection);
    settings.setValue(SER_SWAPFACEBUTTONS, swapFaceButtons);
    settings.setValue(SER_CAPTURESYSKEYS, captureSysKeysMode);
    settings.setValue(SER_KEEPAWAKE, keepAwake);

    settings.endGroup();

    // Force flush to ensure settings are persisted before restoreSettings() runs
    settings.sync();

    if (settings.status() != QSettings::NoError) {
        qWarning() << "QSettings sync error after saving client settings! Status:" << settings.status();
    }

    m_CurrentClientUuid = clientUuid;

    qInfo() << "Saved client-specific settings for UUID:" << clientUuid
            << "resolution:" << width << "x" << height
            << "fps:" << fps
            << "codec:" << static_cast<int>(videoCodecConfig)
            << "bitrate:" << bitrateKbps << "kbps";
}

void StreamingPreferences::resetClientSettings(QString clientUuid)
{
    if (clientUuid.isEmpty()) {
        qWarning() << "Attempted to reset settings for empty client UUID";
        return;
    }

    QSettings settings;
    settings.beginGroup("clients");
    settings.remove(clientUuid);
    settings.endGroup();

    if (m_CurrentClientUuid == clientUuid) {
        m_CurrentClientUuid.clear();
        reload(); // Reload global settings
    }

    qInfo() << "Reset client-specific settings for UUID:" << clientUuid;
}

bool StreamingPreferences::hasClientSettings(QString clientUuid)
{
    if (clientUuid.isEmpty()) {
        qWarning() << "hasClientSettings called with empty UUID";
        return false;
    }

    QSettings settings;
    settings.beginGroup("clients/" + clientUuid);
    QStringList keys = settings.childKeys();
    bool hasSettings = !keys.isEmpty();
    settings.endGroup();

    qInfo() << "hasClientSettings for UUID:" << clientUuid
            << "result:" << hasSettings
            << "keys found:" << keys.count();

    return hasSettings;
}

QVariantMap StreamingPreferences::snapshotSettings() const
{
    QVariantMap map;
    map[QStringLiteral("width")] = width;
    map[QStringLiteral("height")] = height;
    map[QStringLiteral("fps")] = fps;
    map[QStringLiteral("bitrateKbps")] = bitrateKbps;
    map[QStringLiteral("unlockBitrate")] = unlockBitrate;
    map[QStringLiteral("autoAdjustBitrate")] = autoAdjustBitrate;
    map[QStringLiteral("enableVsync")] = enableVsync;
    map[QStringLiteral("gameOptimizations")] = gameOptimizations;
    map[QStringLiteral("playAudioOnHost")] = playAudioOnHost;
    map[QStringLiteral("multiController")] = multiController;
    map[QStringLiteral("enableMdns")] = enableMdns;
    map[QStringLiteral("quitAppAfter")] = quitAppAfter;
    map[QStringLiteral("absoluteMouseMode")] = absoluteMouseMode;
    map[QStringLiteral("absoluteTouchMode")] = absoluteTouchMode;
    map[QStringLiteral("framePacing")] = framePacing;
    map[QStringLiteral("connectionWarnings")] = connectionWarnings;
    map[QStringLiteral("configurationWarnings")] = configurationWarnings;
    map[QStringLiteral("richPresence")] = richPresence;
    map[QStringLiteral("gamepadMouse")] = gamepadMouse;
    map[QStringLiteral("detectNetworkBlocking")] = detectNetworkBlocking;
    map[QStringLiteral("showPerformanceOverlay")] = showPerformanceOverlay;
    map[QStringLiteral("packetSize")] = packetSize;
    map[QStringLiteral("swapMouseButtons")] = swapMouseButtons;
    map[QStringLiteral("muteOnFocusLoss")] = muteOnFocusLoss;
    map[QStringLiteral("backgroundGamepad")] = backgroundGamepad;
    map[QStringLiteral("reverseScrollDirection")] = reverseScrollDirection;
    map[QStringLiteral("swapFaceButtons")] = swapFaceButtons;
    map[QStringLiteral("keepAwake")] = keepAwake;
    map[QStringLiteral("enableHdr")] = enableHdr;
    map[QStringLiteral("enableYUV444")] = enableYUV444;
    map[QStringLiteral("audioConfig")] = static_cast<int>(audioConfig);
    map[QStringLiteral("videoCodecConfig")] = static_cast<int>(videoCodecConfig);
    map[QStringLiteral("videoDecoderSelection")] = static_cast<int>(videoDecoderSelection);
    map[QStringLiteral("windowMode")] = static_cast<int>(windowMode);
    map[QStringLiteral("captureSysKeysMode")] = static_cast<int>(captureSysKeysMode);
    map[QStringLiteral("uiDisplayMode")] = static_cast<int>(uiDisplayMode);
    map[QStringLiteral("language")] = static_cast<int>(language);
    return map;
}

void StreamingPreferences::restoreSettings(const QVariantMap& map)
{
    width = map.value(QStringLiteral("width"), width).toInt();
    height = map.value(QStringLiteral("height"), height).toInt();
    fps = map.value(QStringLiteral("fps"), fps).toInt();
    bitrateKbps = map.value(QStringLiteral("bitrateKbps"), bitrateKbps).toInt();
    unlockBitrate = map.value(QStringLiteral("unlockBitrate"), unlockBitrate).toBool();
    autoAdjustBitrate = map.value(QStringLiteral("autoAdjustBitrate"), autoAdjustBitrate).toBool();
    enableVsync = map.value(QStringLiteral("enableVsync"), enableVsync).toBool();
    gameOptimizations = map.value(QStringLiteral("gameOptimizations"), gameOptimizations).toBool();
    playAudioOnHost = map.value(QStringLiteral("playAudioOnHost"), playAudioOnHost).toBool();
    multiController = map.value(QStringLiteral("multiController"), multiController).toBool();
    enableMdns = map.value(QStringLiteral("enableMdns"), enableMdns).toBool();
    quitAppAfter = map.value(QStringLiteral("quitAppAfter"), quitAppAfter).toBool();
    absoluteMouseMode = map.value(QStringLiteral("absoluteMouseMode"), absoluteMouseMode).toBool();
    absoluteTouchMode = map.value(QStringLiteral("absoluteTouchMode"), absoluteTouchMode).toBool();
    framePacing = map.value(QStringLiteral("framePacing"), framePacing).toBool();
    connectionWarnings = map.value(QStringLiteral("connectionWarnings"), connectionWarnings).toBool();
    configurationWarnings = map.value(QStringLiteral("configurationWarnings"), configurationWarnings).toBool();
    richPresence = map.value(QStringLiteral("richPresence"), richPresence).toBool();
    gamepadMouse = map.value(QStringLiteral("gamepadMouse"), gamepadMouse).toBool();
    detectNetworkBlocking = map.value(QStringLiteral("detectNetworkBlocking"), detectNetworkBlocking).toBool();
    showPerformanceOverlay = map.value(QStringLiteral("showPerformanceOverlay"), showPerformanceOverlay).toBool();
    packetSize = map.value(QStringLiteral("packetSize"), packetSize).toInt();
    swapMouseButtons = map.value(QStringLiteral("swapMouseButtons"), swapMouseButtons).toBool();
    muteOnFocusLoss = map.value(QStringLiteral("muteOnFocusLoss"), muteOnFocusLoss).toBool();
    backgroundGamepad = map.value(QStringLiteral("backgroundGamepad"), backgroundGamepad).toBool();
    reverseScrollDirection = map.value(QStringLiteral("reverseScrollDirection"), reverseScrollDirection).toBool();
    swapFaceButtons = map.value(QStringLiteral("swapFaceButtons"), swapFaceButtons).toBool();
    keepAwake = map.value(QStringLiteral("keepAwake"), keepAwake).toBool();
    enableHdr = map.value(QStringLiteral("enableHdr"), enableHdr).toBool();
    enableYUV444 = map.value(QStringLiteral("enableYUV444"), enableYUV444).toBool();
    audioConfig = static_cast<AudioConfig>(map.value(QStringLiteral("audioConfig"), static_cast<int>(audioConfig)).toInt());
    videoCodecConfig = static_cast<VideoCodecConfig>(map.value(QStringLiteral("videoCodecConfig"), static_cast<int>(videoCodecConfig)).toInt());
    videoDecoderSelection = static_cast<VideoDecoderSelection>(map.value(QStringLiteral("videoDecoderSelection"), static_cast<int>(videoDecoderSelection)).toInt());
    windowMode = static_cast<WindowMode>(map.value(QStringLiteral("windowMode"), static_cast<int>(windowMode)).toInt());
    captureSysKeysMode = static_cast<CaptureSysKeysMode>(map.value(QStringLiteral("captureSysKeysMode"), static_cast<int>(captureSysKeysMode)).toInt());
    uiDisplayMode = static_cast<UIDisplayMode>(map.value(QStringLiteral("uiDisplayMode"), static_cast<int>(uiDisplayMode)).toInt());
    language = static_cast<Language>(map.value(QStringLiteral("language"), static_cast<int>(language)).toInt());

    m_CurrentClientUuid.clear();
    emitAllChanged();

    qInfo() << "Restored settings from snapshot";
}

void StreamingPreferences::emitAllChanged()
{
    emit displayModeChanged();
    emit bitrateChanged();
    emit unlockBitrateChanged();
    emit autoAdjustBitrateChanged();
    emit enableVsyncChanged();
    emit gameOptimizationsChanged();
    emit playAudioOnHostChanged();
    emit multiControllerChanged();
    emit enableMdnsChanged();
    emit quitAppAfterChanged();
    emit absoluteMouseModeChanged();
    emit absoluteTouchModeChanged();
    emit audioConfigChanged();
    emit videoCodecConfigChanged();
    emit enableHdrChanged();
    emit enableYUV444Changed();
    emit videoDecoderSelectionChanged();
    emit uiDisplayModeChanged();
    emit windowModeChanged();
    emit framePacingChanged();
    emit connectionWarningsChanged();
    emit configurationWarningsChanged();
    emit richPresenceChanged();
    emit gamepadMouseChanged();
    emit detectNetworkBlockingChanged();
    emit showPerformanceOverlayChanged();
    emit mouseButtonsChanged();
    emit muteOnFocusLossChanged();
    emit backgroundGamepadChanged();
    emit reverseScrollDirectionChanged();
    emit swapFaceButtonsChanged();
    emit captureSysKeysModeChanged();
    emit keepAwakeChanged();
    emit languageChanged();
    emit appSortModeChanged();
    emit appViewModeChanged();
    emit appTileScaleChanged();
    emit pcSortModeChanged();
    emit pcTileScaleChanged();
    emit pcShowSectionsChanged();
    emit showPcInfoChanged();
}

// ---- Custom Order Management ----

QStringList StreamingPreferences::getAppCustomOrder(const QString& computerUuid) const
{
    QSettings settings;
    return settings.value("appCustomOrder/" + computerUuid).toStringList();
}

void StreamingPreferences::setAppCustomOrder(const QString& computerUuid, const QStringList& appIds)
{
    QSettings settings;
    settings.setValue("appCustomOrder/" + computerUuid, appIds);
    settings.sync();
}

QStringList StreamingPreferences::getPcCustomOrder() const
{
    QSettings settings;
    return settings.value("pcCustomOrder").toStringList();
}

void StreamingPreferences::setPcCustomOrder(const QStringList& pcUuids)
{
    QSettings settings;
    settings.setValue("pcCustomOrder", pcUuids);
    settings.sync();
}

// ---- Folder Management ----

QStringList StreamingPreferences::getAppFolders(const QString& computerUuid) const
{
    QSettings settings;
    settings.beginGroup("appFolders/" + computerUuid);
    QStringList folders = settings.childGroups();
    settings.endGroup();
    return folders;
}

void StreamingPreferences::createAppFolder(const QString& computerUuid, const QString& folderName)
{
    QSettings settings;
    settings.beginGroup("appFolders/" + computerUuid + "/" + folderName);
    settings.setValue("created", true);
    if (!settings.contains("apps")) {
        settings.setValue("apps", QStringList());
    }
    settings.endGroup();
    settings.sync();
}

void StreamingPreferences::deleteAppFolder(const QString& computerUuid, const QString& folderName)
{
    QSettings settings;
    settings.beginGroup("appFolders/" + computerUuid);
    settings.remove(folderName);
    settings.endGroup();
    settings.sync();
}

void StreamingPreferences::renameAppFolder(const QString& computerUuid, const QString& oldName, const QString& newName)
{
    QSettings settings;
    // Read apps from old folder
    settings.beginGroup("appFolders/" + computerUuid + "/" + oldName);
    QStringList apps = settings.value("apps").toStringList();
    settings.endGroup();

    // Create new folder with same apps
    settings.beginGroup("appFolders/" + computerUuid + "/" + newName);
    settings.setValue("created", true);
    settings.setValue("apps", apps);
    settings.endGroup();

    // Delete old folder
    settings.beginGroup("appFolders/" + computerUuid);
    settings.remove(oldName);
    settings.endGroup();
    settings.sync();
}

QStringList StreamingPreferences::getAppsInFolder(const QString& computerUuid, const QString& folderName) const
{
    QSettings settings;
    settings.beginGroup("appFolders/" + computerUuid + "/" + folderName);
    QStringList apps = settings.value("apps").toStringList();
    settings.endGroup();
    return apps;
}

void StreamingPreferences::setAppsInFolder(const QString& computerUuid, const QString& folderName, const QStringList& appIds)
{
    QSettings settings;
    settings.beginGroup("appFolders/" + computerUuid + "/" + folderName);
    settings.setValue("apps", appIds);
    settings.endGroup();
    settings.sync();
}

void StreamingPreferences::addAppToFolder(const QString& computerUuid, const QString& folderName, const QString& appId)
{
    QStringList apps = getAppsInFolder(computerUuid, folderName);
    if (!apps.contains(appId)) {
        apps.append(appId);
        setAppsInFolder(computerUuid, folderName, apps);
    }
}

void StreamingPreferences::removeAppFromFolder(const QString& computerUuid, const QString& folderName, const QString& appId)
{
    QStringList apps = getAppsInFolder(computerUuid, folderName);
    apps.removeAll(appId);
    setAppsInFolder(computerUuid, folderName, apps);
}

QString StreamingPreferences::getAppFolder(const QString& computerUuid, const QString& appId) const
{
    QStringList folders = getAppFolders(computerUuid);
    for (const QString& folder : folders) {
        QStringList apps = getAppsInFolder(computerUuid, folder);
        if (apps.contains(appId)) {
            return folder;
        }
    }
    return QString();
}

// ---- Custom Shortcut Management ----

QVariantList StreamingPreferences::getCustomShortcuts() const
{
    QVariantList result;
    QSettings settings;
    settings.beginGroup("shortcuts");
    QStringList actions = settings.childKeys();
    for (const QString& action : actions) {
        QVariantMap entry;
        entry["action"] = action;
        entry["shortcut"] = settings.value(action).toString();
        result.append(entry);
    }
    settings.endGroup();
    return result;
}

void StreamingPreferences::setCustomShortcut(const QString& action, const QString& shortcut)
{
    if (action.isEmpty()) return;

    QSettings settings;
    settings.beginGroup("shortcuts");
    if (shortcut.isEmpty()) {
        settings.remove(action);
    } else {
        settings.setValue(action, shortcut);
    }
    settings.endGroup();
    settings.sync();

    qInfo() << "Set custom shortcut:" << action << "=" << shortcut;
}

void StreamingPreferences::removeCustomShortcut(const QString& action)
{
    if (action.isEmpty()) return;

    QSettings settings;
    settings.beginGroup("shortcuts");
    settings.remove(action);
    settings.endGroup();
    settings.sync();

    qInfo() << "Removed custom shortcut for action:" << action;
}

QString StreamingPreferences::getShortcutForAction(const QString& action) const
{
    QSettings settings;
    settings.beginGroup("shortcuts");
    QString shortcut = settings.value(action).toString();
    settings.endGroup();
    return shortcut;
}

QStringList StreamingPreferences::getAvailableShortcutActions() const
{
    QStringList actions;
    actions << "quit_stream"
            << "toggle_perf_overlay"
            << "toggle_fullscreen"
            << "toggle_mouse_capture"
            << "disconnect_stream"
            << "toggle_mute"
            << "toggle_minimize";
    return actions;
}
