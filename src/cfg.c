/**

 base path:   HKLM\SYSTEM\CurrentControlSet\Services\Integra\
    - \Parameters                  - subkey. if not exists, create
    - \Parameters\ObjectListFile   - REG_SZ. required (exit if not present)

 */

#include <windows.h>
#include <tchar.h>
#include "cfg.h"


#define BASE_PATH _T("SYSTEM\\CurrentControlSet\\Services\\") SVCNAME
#define PARAMETERS_PATH BASE_PATH _T("\\Parameters")
#define OL_FILE _T("ObjectListFile")
#define CHECK_INTERVAL _T("CheckIntervalMS")


LPTSTR GetOLFilePath() {
    /**
     * @brief Allocate and Read REG_SZ: Parameters/ObjectListFile.
     * Allocates result string, so the caller is responsible for freeing.
     */
    HKEY parametersKey;
    DWORD valueType;
    DWORD bufferSize = 0;

    if (ERROR_SUCCESS != RegCreateKeyEx(HKEY_LOCAL_MACHINE, PARAMETERS_PATH, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ, NULL, &parametersKey, NULL))
        return FALSE;  // Failed to create or open parameters key


    if (ERROR_SUCCESS != RegQueryValueEx(parametersKey, OL_FILE, NULL, &valueType, NULL, &bufferSize)) {
        RegCloseKey(parametersKey);
        return NULL;  // Failed to get log file value size
    }

    if (valueType != REG_SZ) {
        RegCloseKey(parametersKey);
        return NULL;  // Log file value is not REG_SZ
    }

    LPTSTR filePath = calloc(bufferSize, sizeof(TCHAR));
    if (!filePath) {
        RegCloseKey(parametersKey);
        return NULL;
    }

    if (ERROR_SUCCESS != RegQueryValueEx(parametersKey, OL_FILE, NULL, NULL, (LPVOID) filePath, &bufferSize)) {
        free(filePath);
        RegCloseKey(parametersKey);
        return NULL;  // Failed to get log file value
    }

    RegCloseKey(parametersKey);
    return filePath;
}


WINBOOL SetOLFilePath(LPCTSTR path) {
    /**
     * @brief Create or set REG_SZ at Parameters/ObjectListFile
     */
    HKEY parametersKey;
    if (ERROR_SUCCESS != RegCreateKeyEx(HKEY_LOCAL_MACHINE, PARAMETERS_PATH, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &parametersKey, NULL))
        return FALSE;  // Failed to create or open parameters key

    WINBOOL result = (RegSetValueEx(parametersKey,
                                    OL_FILE,
                                    0,
                                    REG_SZ,
                                    (LPVOID) path,
                                    (_tcslen(path) + 1) * sizeof(TCHAR)
    ) == ERROR_SUCCESS);

    RegCloseKey(parametersKey);
    return result;
}


WINBOOL InitRegPaths() {
    /**
     * @brief Create sub-key Parameters
     */
    HKEY paramsKey;

    // Create (if needed) Parameters
    if (ERROR_SUCCESS != RegOpenKeyEx(HKEY_LOCAL_MACHINE, PARAMETERS_PATH, 0, KEY_READ, &paramsKey)) {
        if (ERROR_SUCCESS != RegCreateKeyEx(HKEY_LOCAL_MACHINE,PARAMETERS_PATH, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &paramsKey, NULL))
            return FALSE;  // Failed to create or open key
        RegCloseKey(paramsKey);
    }

    return TRUE;  // Key was initialized
}



DWORD GetCheckInterval() {
    /**
     * @brief Read DWORD: Parameters/CheckIntervalMS
     */
    HKEY parametersKey;
    DWORD dwValue = 0, dwSize = sizeof(DWORD);

    if (ERROR_SUCCESS != RegCreateKeyEx(HKEY_LOCAL_MACHINE, PARAMETERS_PATH, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ, NULL, &parametersKey, NULL))
        return 0;  // Failed to create or open parameters key


    if (ERROR_SUCCESS != RegQueryValueEx(parametersKey, CHECK_INTERVAL, NULL, NULL, (LPVOID) &dwValue, &dwSize)) {
        RegCloseKey(parametersKey);
        return 0;
    }

    RegCloseKey(parametersKey);
    return dwValue;
}

WINBOOL SetCheckInterval(DWORD dwValueMs) {
    /**
     * @brief Create or set REG_DWORD at Parameters/CheckIntervalMS
     */
    if (!dwValueMs) return FALSE;

    HKEY parametersKey;
    if (ERROR_SUCCESS != RegCreateKeyEx(HKEY_LOCAL_MACHINE, PARAMETERS_PATH, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &parametersKey, NULL))
        return FALSE;  // Failed to create or open parameters key

    WINBOOL result = (RegSetValueEx(parametersKey,
                                    CHECK_INTERVAL,
                                    0,
                                    REG_SZ,
                                    (LPVOID) &dwValueMs,
                                    sizeof(DWORD)
                     ) == ERROR_SUCCESS);

    RegCloseKey(parametersKey);
    return result;
}