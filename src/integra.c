#include <stdio.h>
#include "integra.h"
#include "cfg.h"
#include "event.h"

// default: 30 minutes
#ifndef CHECK_DELAY_MS
#define CHECK_DELAY_MS 30 * 60 * 1000
#endif

// default: 100 MB
#ifndef MAX_JSON_SIZE
#define MAX_JSON_SIZE 104857600
#endif

#define OBJECT_FILE 0
#define OBJECT_REGISTRY 1

#define BUF_LEN 64


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


void ServiceLoop(HANDLE stopEvent) {
    /**
     * @brief Main loop for service. Truly main.
     *
     * @details sleep for delay, then perform hash check based on object list (path in registry, cfg.h)
     */

    // Read path to OL from registry
    LPTSTR olPath = GetOLFilePath();
    if (!olPath) {
        SvcReportEvent(EVENTLOG_ERROR_TYPE, "Object List file is not set. Please run (as admin):\n"
               "\tintegra list path <path>"
               "\n  where <path> is absolute path to store Object List at (ex. C:\\path\\objects.json)\n");
        return;
    }

    DWORD res, dwNumObjects;
    while (TRUE) {

        // Read Object List
        cJSON* jsonObjectList = ReadJSON(olPath);
        if (!jsonObjectList || !cJSON_IsArray(jsonObjectList)) return;

        // Verify objects
        dwNumObjects = cJSON_GetArraySize(jsonObjectList);
        for (int i = 0; i < dwNumObjects; i++)
            VerifyObject(cJSON_GetArrayItem(jsonObjectList, i));

        cJSON_Delete(jsonObjectList);

        // Sleep for Check Delay while listening for stop signal
        res = WaitForSingleObject(stopEvent, CHECK_DELAY_MS);
        if (res != WAIT_TIMEOUT) return;
    }
}


#define ReportObjErrorAndRet() \
    do { \
        snprintf(buf, BUF_LEN-1, "Verification for object '%s': Failed (bad JSON)", szObjectName);   \
        SvcReportEvent(EVENTLOG_ERROR_TYPE, buf);                                           \
        return FALSE;                                                                       \
    } while (0)


WINBOOL VerifyObject(cJSON* jsonObject) {
    /**
     * @brief Verify HashTree of object against actual object
     *
     * @details Given object as cJSON:
     *      string  object_name
     *      WORD    type
     *      string  path
     *      cJSON   root
     *
     *  Where root is the root HashNode of HashTree, given as cJSON:
     *      string  name    -(for root)
     *      string  hash    -(skip hash check?)
     *      [cJSON] slaves
     */

    TCHAR buf[BUF_LEN];
    LPTSTR szObjectName = NULL;

    cJSON* jsonObjectName = cJSON_GetObjectItem(jsonObject, "object_name");
    if (jsonObjectName && cJSON_IsString(jsonObjectName))
        szObjectName = cJSON_GetStringValue(jsonObjectName);

    if (!szObjectName) szObjectName = "Unnamed";

    snprintf(buf, BUF_LEN-1, "Started verification for object '%s'", szObjectName);
    SvcReportEvent(EVENTLOG_INFORMATION_TYPE, buf);

    cJSON* jsonPath = cJSON_GetObjectItem(jsonObject, "path");
    if (!jsonPath || !cJSON_IsString(jsonPath)) ReportObjErrorAndRet();
    LPTSTR szPath = cJSON_GetStringValue(jsonPath);

    cJSON* jsonType = cJSON_GetObjectItem(jsonObject, "type");
    if (!jsonType || !cJSON_IsNumber(jsonType)) ReportObjErrorAndRet();
    DWORD dwType = (DWORD) cJSON_GetNumberValue(jsonType);

    cJSON* jsonRootNode = cJSON_GetObjectItem(jsonObject, "root");
    if (!jsonRootNode) ReportObjErrorAndRet();

    // Actual verification
    return VerifyNode(jsonRootNode, dwType, szPath);
}


#define ReportNodeErrorAndRet() \
    do { \
        snprintf(buf, BUF_LEN-1, "Path '%s\\%s': Failed (bad JSON)", szPath, szName ? szName : "<unknown>");  \
        SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);                                                  \
        return FALSE;                                                                                \
    } while (0)

WINBOOL VerifyNode(cJSON* jsonNode, DWORD dwType, LPCTSTR szPath) {
    /**
     * @brief Verify HashNode against actual sub-folder, file, sub-key, or value
     *
     * @param szPath base path where Node is located
     * @param dwType object type: OBJECT_REGISTRY or OBJECT_FILE
     *
     * @details TODO
     */

    TCHAR buf[BUF_LEN];

    LPTSTR szName = NULL;
    cJSON* jsonName = cJSON_GetObjectItem(jsonNode, "name");
    if (jsonName && cJSON_IsString(jsonName))
        szName = cJSON_GetStringValue(jsonName);

    snprintf(buf, BUF_LEN-1, "Path '%s\\%s': OK", szPath, szName);
    SvcReportEvent(EVENTLOG_INFORMATION_TYPE, buf);
    return TRUE;
}