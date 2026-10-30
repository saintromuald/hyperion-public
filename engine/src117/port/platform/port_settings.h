// port_settings.h -- Hyperion's settings file (port_settings.cpp, docs/front-end.md, F3).
// Presentation only.
#ifndef PORT_SETTINGS_H
#define PORT_SETTINGS_H

#include <stddef.h>

// One value of the file: a section ("hyperion" for Hyperion's own settings, or a key 1.17 keeps
// its settings under, such as "Starcraft") and a name in it. The file is read on first use and
// written whenever a value changes.
enum PortSettingKind { PORT_SETTING_NONE, PORT_SETTING_NUMBER, PORT_SETTING_STRING, PORT_SETTING_DATA };
PortSettingKind PortSettingKindOf(const char *pszSection, const char *pszName);
bool PortSettingNumber(const char *pszSection, const char *pszName, double *pdValue);
bool PortSettingString(const char *pszSection, const char *pszName, char *pszBuf, size_t cchBuf);
bool PortSettingData(const char *pszSection, const char *pszName, void *pvBuf, size_t cbBuf, size_t *pcbData,
                     bool *pbMulti);
void PortSettingSetNumber(const char *pszSection, const char *pszName, double dValue);
void PortSettingSetString(const char *pszSection, const char *pszName, const char *pszValue);
void PortSettingSetData(const char *pszSection, const char *pszName, const void *pvData, size_t cbData, bool bMulti);
bool PortSettingRemove(const char *pszSection, const char *pszName);

// Hold writes while several values change together (the settings screen's Accept, 1.17's
// exit saving its sixteen preferences), then write the file once.
void PortSettingsHold(void);
void PortSettingsRelease(void);

// Where the file is: settings.json in the player data folder (PortDataFolder).
const char *PortSettingsPath(void);

// How the file read: missing, its size and values, or the line its reading stopped at.
const char *PortSettingsReadNote(void);

#endif
