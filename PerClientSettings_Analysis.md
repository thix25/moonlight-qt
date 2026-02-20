# Per-Client Settings Integration — Code Analysis Report

**Date:** February 19, 2026  
**Branch:** `feature/perClientSettings` (merged into `master` at `5d448e07`)  
**Commits analyzed:** `044532f3` → `b2e2f716` (11 commits)

---

## 1. Overview — What Was Done

The feature adds the ability to configure **per-PC streaming settings** in Moonlight QT, so that different host PCs can have different streaming preferences (resolution, FPS, codec, input settings, etc.) instead of sharing a single global configuration.

### Files Modified

| File | Change |
|------|--------|
| `app/settings/streamingpreferences.h` | Added `loadForClient()`, `saveForClient()`, `resetClientSettings()`, `hasClientSettings()`, `currentClientUuid()` methods and `m_CurrentClientUuid` member |
| `app/settings/streamingpreferences.cpp` | Implemented the 4 new methods (~150 lines). Added guard in `save()` and `reload()` to handle client UUID state |
| `app/gui/ClientSettingsDialog.qml` | **New file** (741 lines) — Full dialog UI for per-PC settings |
| `app/gui/PcView.qml` | Added "PC Settings" context menu item and `ClientSettingsDialog` instance |
| `app/gui/computermodel.h` / `.cpp` | Exposed `UuidRole` to QML so the PC's UUID is available |
| `app/streaming/session.cpp` | Auto-loads client-specific settings when starting a stream |
| `app/qml.qrc` | Registered `ClientSettingsDialog.qml` |
| `app/gui/SettingsView.qml` | Added 8K resolution and 90/100/120/144 FPS options (bonus, not core to per-client) |

---

## 2. Architecture Analysis

### 2.1 Storage Design ✅ Good
Settings are stored under `QSettings` groups using the path `clients/<uuid>/`. This is clean and leverages Qt's existing settings infrastructure. Each client UUID gets its own namespace under the `clients` group.

### 2.2 Singleton Mutation Pattern ⚠️ Problematic  
`StreamingPreferences` is a **global singleton** (`s_GlobalPrefs`). The per-client feature works by **mutating the singleton's fields in-place** and tracking state via `m_CurrentClientUuid`. This is the root cause of most bugs in this implementation.

The flow is:
1. Save current global values into a JavaScript object (`originalGlobalSettings`)
2. Call `loadForClient()` which overwrites the singleton's fields
3. On accept: call `saveForClient()`, then manually restore all fields, then `reload()`
4. On reject: manually restore all fields, then `reload()`

This creates a fragile manual backup/restore cycle repeated **3 times** in the QML (onOpened backup, onAccepted restore, onRejected restore) — ~35 properties each time.

---

## 3. Bugs Found

### 🐛 BUG 1: Global settings corruption after streaming with per-client settings (Critical)

**Location:** [session.cpp](app/streaming/session.cpp#L592-L598)

```cpp
if (!preferences && m_Computer && m_Preferences->hasClientSettings(m_Computer->uuid)) {
    m_Preferences->loadForClient(m_Computer->uuid);
    m_IsFullScreen = (...);
}
```

When a streaming session starts from the GUI (via `AppModel::createSessionForApp`), the `preferences` parameter is `nullptr`, so `m_Preferences` points to the **global singleton**. Calling `loadForClient()` **permanently mutates the global singleton** with per-client values and sets `m_CurrentClientUuid`.

After streaming ends, **nothing restores the global settings**. The `sessionFinished` handler in `StreamSegue.qml` doesn't call `reload()`. This means:

- The global settings page (SettingsView) will display the per-client values
- Worse, when the user navigates to SettingsView and it calls `StreamingPreferences.save()` on `StackView.onDeactivating`, the `save()` method **silently refuses to save** because `m_CurrentClientUuid` is non-empty (it just prints a warning). This means any changes the user makes in the global settings page are **lost**.
- Starting a stream to a **different** PC without per-client settings will use the **previous PC's** per-client values as the base (since `reload()` was never called).

**Impact:** After streaming to a PC with custom settings, the entire application's settings state is corrupted until Moonlight is restarted.

### 🐛 BUG 2: Missing `autoAdjustBitrate` in UI but tracked in backup/restore (Minor)

**Location:** [ClientSettingsDialog.qml](app/gui/ClientSettingsDialog.qml)

The `autoAdjustBitrate` setting is backed up in `originalGlobalSettings` and restored on cancel/accept, and it's saved/loaded by `saveForClient()`/`loadForClient()`. However, there is **no UI control** for it in the dialog. 

In the main SettingsView, `autoAdjustBitrate` is implicitly toggled when the user manually moves the bitrate slider (`onMoved` sets it to `false`) or clicks "Use Default" (sets it to `true`). The ClientSettingsDialog's bitrate slider has no such logic. This means:
- The auto-adjust bitrate behavior is silently inherited from global settings
- The user cannot toggle it per-client
- If a user's global auto-adjust is `true`, saving per-client settings will always save `true` for this field, and the bitrate they set in the slider may get auto-adjusted away on next load

### 🐛 BUG 3: `packetSize` not exposed in dialog UI but saved/loaded inconsistently (Minor)

`packetSize` is loaded in `loadForClient()` and saved in `saveForClient()`, but:
- It has **no UI control** in the dialog
- It is **not backed up** in `originalGlobalSettings`  
- It is **not restored** on accept/reject

This means the `packetSize` value can drift silently between client sessions.

### 🐛 BUG 4: Missing properties in `saveForClient()` vs `loadForClient()` (Minor)

Comparing `loadForClient()` to `saveForClient()`:
- `loadForClient()` loads `autoAdjustBitrate` ✅
- `saveForClient()` does **not** save `autoAdjustBitrate` ❌

This means `autoAdjustBitrate` will always fallback to the global value when loading per-client settings, creating inconsistent behavior.

### 🐛 BUG 5: Resolution and FPS dropdowns are hardcoded, not matching SettingsView logic (Medium)

**Location:** [ClientSettingsDialog.qml](app/gui/ClientSettingsDialog.qml#L332-L370)

The dialog uses hardcoded resolution entries (720p, 1080p, 1440p, 4K, 8K) and FPS entries (30, 60, 90, 100, 120, 144). The main SettingsView (`SettingsView.qml`) has dynamic logic:
- It includes the **native display resolution** as an option
- It includes custom resolutions
- FPS values above 60 are only shown if the display supports them (`SystemProperties.maximumStreamingFrameRate`)
- It includes an "unsupported FPS" checkbox that gates higher frame rates

The per-client dialog ignores all of this. A user could set 8K@144FPS for a client even if their hardware doesn't support it, with no warnings.

### 🐛 BUG 6: Codec enum values are hardcoded and may be wrong (Minor)

**Location:** [ClientSettingsDialog.qml](app/gui/ClientSettingsDialog.qml#L381-L388)

```qml
ListElement { text: "Automatic"; val: 0 }
ListElement { text: "H.264"; val: 1 }
ListElement { text: "HEVC (H.265)"; val: 2 }
ListElement { text: "AV1"; val: 4 }
```

The enum values are hardcoded integers matching `VCC_AUTO=0`, `VCC_FORCE_H264=1`, `VCC_FORCE_HEVC=2`, `VCC_FORCE_AV1=4`. Value `3` (`VCC_FORCE_HEVC_HDR_DEPRECATED`) is correctly skipped, but using raw integers instead of the `StreamingPreferences.VCC_*` enum constants is fragile. If the enum order ever changes, this will silently break.

The same applies to decoder, window mode, audio config, and capture sys keys mode lists.

### 🐛 BUG 7: `updateUIFromPreferences()` inverted touch mode logic (Potential)

**Location:** [ClientSettingsDialog.qml](app/gui/ClientSettingsDialog.qml#L215)

```qml
absoluteTouchCheckbox.checked = !StreamingPreferences.absoluteTouchMode
```

And on save:
```qml
StreamingPreferences.absoluteTouchMode = !absoluteTouchCheckbox.checked
```

The checkbox text is "Use touchscreen as a virtual trackpad". The negation matches the SettingsView behavior — `absoluteTouchMode = true` means direct touch, not trackpad. So checking "virtual trackpad" means `absoluteTouchMode = false`. This is **correct but confusing** and worth documenting.

### 🐛 BUG 8: No `NOTIFY` signal emissions after `loadForClient()` (Medium)

**Location:** [streamingpreferences.cpp](app/settings/streamingpreferences.cpp#L425-L490)

The `loadForClient()` and `reload()` methods directly modify all `Q_PROPERTY` members but **never emit** the associated `NOTIFY` signals. Since `Q_PROPERTY` uses `MEMBER` binding, QML bindings that read these properties won't be notified of changes. 

In the dialog this is mitigated because `updateUIFromPreferences()` manually reads and sets all UI controls. But if any other QML component is bound to `StreamingPreferences` properties (like the SettingsView if it's still loaded in the StackView), those bindings will have stale values.

---

## 4. Design Concerns

### 4.1 Massive Code Duplication
The 35+ property backup/restore is written out **3 complete times** in the QML (onOpened, onAccepted, onRejected). Any new setting added to StreamingPreferences requires updating **7 places**: `loadForClient()`, `saveForClient()`, `originalGlobalSettings` backup, `onAccepted` restore, `onRejected` restore, dialog UI, and `updatePreferencesFromUI()`.

**Recommendation:** Create a helper method in C++ (e.g., `snapshotSettings()` / `restoreSettings()`) that returns/accepts a `QVariantMap`, eliminating the manual field-by-field copy in QML.

### 4.2 Singleton Mutation is Error-Prone
The core design of mutating the global singleton to temporarily hold per-client values is fragile. Any code path that reads or writes `StreamingPreferences` during the dialog interaction will see corrupted state.

**Recommendation:** Either:
- Create a **separate `StreamingPreferences` instance** for per-client editing (not the singleton)
- Or keep the dialog purely local: read/write client settings via a `QVariantMap` without ever touching the singleton's members

### 4.3 UI Completeness
The per-client dialog provides a good subset of settings but is missing several settings that exist in the global SettingsView:
- `autoAdjustBitrate` (toggle / "Use Default" button)
- `packetSize`
- `language` (though per-client language makes less sense)
- `uiDisplayMode` (UI display mode)
- Native resolution detection
- Unsupported FPS/resolution guards
- Gamepad mapping button

### 4.4 Thread Safety
`loadForClient()` and `saveForClient()` operate on the global singleton which is protected by `s_GlobalPrefsLock` in `get()` but **not** in the load/save methods themselves. Since `Session` can call `loadForClient()` from the streaming initialization path while the QML UI thread reads properties, there's a potential race condition.

---

## 5. What Works Well

| Aspect | Assessment |
|--------|-----------|
| QSettings group structure (`clients/<uuid>`) | Clean, scalable |
| UUID exposure via `ComputerModel::UuidRole` | Correct approach |
| Guard in `save()` preventing accidental global overwrite | Good safety measure |
| "Use global settings" checkbox toggle | Good UX pattern |
| Restore Defaults button via `Dialog.RestoreDefaults` | Nice QML integration |
| Fallback to global values when loading client settings | Correct design |
| `hasClientSettings()` check before loading | Efficient |
| The dialog is well-organized with grouped settings | Clean UX |

---

## 6. Summary

| Category | Count |
|----------|-------|
| Critical Bugs | 1 (global state corruption after streaming) |
| Medium Bugs | 2 (hardcoded enums, missing signal emissions) |
| Minor Bugs | 4 (missing UI controls, inconsistent save/load, codec values) |
| Design Concerns | 4 (code duplication, singleton mutation, UI completeness, thread safety) |

**Overall Assessment:** The feature's **concept is sound** and the storage design is good. However, the implementation has a **critical bug** where the global singleton gets permanently corrupted after streaming to a PC with custom settings. The manual backup/restore pattern in QML is fragile and will cause maintenance headaches. The recommended fix is to avoid mutating the global singleton and instead use a separate preferences instance or `QVariantMap` for the dialog and session.
