#include <stdio.h>
#include <tchar.h>
#include "event.h"
#include "utils.h"
#include "cfg.h"
#include "snapshot.h"

// default: 100 MB
#ifndef MAX_JSON_SIZE
#define MAX_JSON_SIZE 104857600
#endif

#define OBJECT_FILE 0
#define OBJECT_REGISTRY 1

#define NOT_FOUND (-1)


HKEY ParseRootHKEY(LPCTSTR szPath) {
    LPTSTR lpIndex = strchr(szPath, '\\');
    if (!lpIndex) return INVALID_HANDLE_VALUE;

    DWORD size = lpIndex - szPath;
    if (!strncmp("HKEY_CLASSES_ROOT", szPath, size)) return HKEY_CLASSES_ROOT;
    if (!strncmp("HKEY_CURRENT_CONFIG", szPath, size)) return HKEY_CURRENT_CONFIG;
    if (!strncmp("HKEY_CURRENT_USER", szPath, size)) return HKEY_CURRENT_USER;
    if (!strncmp("HKEY_LOCAL_MACHINE", szPath, size)) return HKEY_LOCAL_MACHINE;
    if (!strncmp("HKEY_USERS", szPath, size)) return HKEY_USERS;

    return INVALID_HANDLE_VALUE;
}


cJSON* ReadJSON(LPCTSTR path) {
    /**
     * @brief Open file and read JSON. Report any errors
     */

    HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, 0, OPEN_EXISTING, 0, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        SvcReportEvent(EVENTLOG_ERROR_TYPE, "Object List file cannot be accessed. Add something to list or change the path");
        return NULL;
    }

    DWORD size = GetFileSize(hFile, NULL);
    if (size > MAX_JSON_SIZE) {
        CloseHandle(hFile);
        SvcReportEvent(EVENTLOG_ERROR_TYPE, "Object List file is too large");
        return NULL;
    }

    LPTSTR buf = calloc(size+1, sizeof(TCHAR));
    if (!buf) { CloseHandle(hFile); return NULL; }

    ReadFile(hFile, buf, size, NULL, NULL);

    cJSON* json = cJSON_ParseWithLength(buf, size+1);
    if (!json) SvcReportEvent(EVENTLOG_ERROR_TYPE, "Failed to parse JSON from Object List file");

    free(buf);
    CloseHandle(hFile);
    return json;
}


#define OpenOL() \
    LPTSTR szOlPath = GetOLFilePath(); \
    if (!szOlPath) { \
        printf("Object List file is not set. Please run (as admin):\n" \
        "\tintegra list path <path>" \
        "\n  where <path> is absolute path to store Object List at (ex. C:\\path\\objects.json)\n"); \
        return EXIT_FAILURE; \
    } \
    cJSON* jsonObjectList = ReadJSON(szOlPath); \
    if (!jsonObjectList || !cJSON_IsArray(jsonObjectList)) return EXIT_FAILURE


#define SaveOL() \
    LPTSTR buf = cJSON_Print(jsonObjectList); \
    if (!buf) printf("Could not serialize JSON!\n"); \
    else { \
        HANDLE hFile = CreateFile(szOlPath, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL); \
        if (hFile == INVALID_HANDLE_VALUE) { \
            free(buf); \
            printf("Could not save JSON to file (%lu)\n", GetLastError()); \
        } \
        else {   \
            WriteFile(hFile, buf, _tcslen(buf), NULL, NULL); \
            CloseHandle(hFile); \
            printf("OK\n"); \
        } \
    }


#define CloseOL() \
    free(szOlPath); \
    cJSON_Delete(jsonObjectList)


int AddObjectToOL(LPCTSTR szName, DWORD dwType, LPCTSTR szPath) {
    /**
     * @brief Snapshot and add object to OL array
     */

    OpenOL();

    if (NOT_FOUND != FindIndexByNameInOL(jsonObjectList, szName)) {
        printf("Object with name '%s' already exists\n", szName);
        CloseOL();
        return EXIT_FAILURE;
    }

    cJSON* jsonObject = SnapshotObject(dwType, szName, szPath);
    if (!jsonObject) {
        CloseOL();
        return EXIT_FAILURE;
    }

    cJSON_AddItemToArray(jsonObjectList, jsonObject);

    SaveOL();
    CloseOL();
    return EXIT_SUCCESS;
}


int RemoveObjectFromOL(LPCTSTR szName) {
    /**
     * @brief Find object by name and remove it from array
     */

    OpenOL();

    int index = FindIndexByNameInOL(jsonObjectList, szName);
    if (NOT_FOUND == index) {
        printf("Object '%s' is not in Object List\n", szName);
        CloseOL();
        return EXIT_FAILURE;
    }

    cJSON_DeleteItemFromArray(jsonObjectList, index);

    SaveOL();
    CloseOL();
    return EXIT_SUCCESS;
}


int UpdateObjectInOL(LPCTSTR szName) {
    /**
     * @brief Re-snapshot object and replace it in array
     */

    OpenOL();

    int index = FindIndexByNameInOL(jsonObjectList, szName);
    if (NOT_FOUND == index) {
        printf("Object '%s' is not in Object List\n", szName);
        CloseOL();
        return EXIT_FAILURE;
    }

    cJSON* jsonObject = cJSON_GetArrayItem(jsonObjectList, index);

    cJSON* jsonType = cJSON_GetObjectItem(jsonObject, "type");
    if (!jsonType || !cJSON_IsNumber(jsonType)) {
        printf("Failed: object type not specified\n");
        CloseOL();
        return EXIT_FAILURE;
    }
    DWORD dwType = cJSON_GetNumberValue(jsonType);

    cJSON* jsonPath = cJSON_GetObjectItem(jsonObject, "path");
    if (!jsonPath || !cJSON_IsString(jsonPath)) {
        printf("Failed: object has no path\n");
        CloseOL();
        return EXIT_FAILURE;
    }
    LPTSTR szPath = cJSON_GetStringValue(jsonPath);

    cJSON* jsonUpdatedObject = SnapshotObject(dwType, szName, szPath);
    if (!jsonUpdatedObject) {
        CloseOL();
        return EXIT_FAILURE;
    }

    cJSON_DeleteItemFromArray(jsonObjectList, index);
    cJSON_AddItemToArray(jsonObjectList, jsonUpdatedObject);

    SaveOL();
    CloseOL();
    return EXIT_SUCCESS;
}


int PrintObjectsInOL() {
    /**
     * @brief Print brief info about all objects in OL (name, type, path)
     */

    DWORD dwType = 0;
    LPTSTR szPath = "<unknown>";
    LPTSTR szName = "<unnamed>";

    OpenOL();
    int size = cJSON_GetArraySize(jsonObjectList);
    printf("Total %d objects:\n", size);

    for (int i = 0; i < size; i++) {
        cJSON* jsonObject = cJSON_GetArrayItem(jsonObjectList, i);

        cJSON* jsonType = cJSON_GetObjectItem(jsonObject, "type");
        if (jsonType && cJSON_IsNumber(jsonType))
            dwType = cJSON_GetNumberValue(jsonType);

        cJSON* jsonPath = cJSON_GetObjectItem(jsonObject, "path");
        if (jsonPath && cJSON_IsString(jsonPath))
            szPath = cJSON_GetStringValue(jsonPath);

        cJSON* jsonName = cJSON_GetObjectItem(jsonObject, "object_name");
        if (jsonPath && cJSON_IsString(jsonName))
            szName = cJSON_GetStringValue(jsonName);

        printf("'%s' \t%s     '%s'\n", szName, dwType == OBJECT_REGISTRY ? "REG " : "FILE", szPath);
    }

    CloseOL();
    return EXIT_SUCCESS;
}

int FindIndexByNameInOL(cJSON* jsonObjectList, LPCTSTR szName) {
    /**
     * @brief Find object's index in OL by its name
     */

    if (!jsonObjectList || !cJSON_IsArray(jsonObjectList)) {
        printf("Failed: Object List has invalid JSON format\n");
        return NOT_FOUND;
    }

    int i;
    cJSON* jsonObject;
    DWORD dwNumObjects = cJSON_GetArraySize(jsonObjectList);

    for (i = 0; i < dwNumObjects; i++) {
        jsonObject = cJSON_GetArrayItem(jsonObjectList, i);
        cJSON* jsonName = cJSON_GetObjectItem(jsonObject, "object_name");
        if (!jsonName || !cJSON_IsString(jsonName)) continue;
        LPTSTR szObjName = cJSON_GetStringValue(jsonName);
        if (!_tcscmp(szObjName, szName))
            return i;
    }

    return NOT_FOUND;
}