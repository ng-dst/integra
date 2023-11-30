#include <stdio.h>
#include <tchar.h>
#include "md5.h"
#include "cfg.h"
#include "event.h"
#include "utils.h"
#include "integra.h"

// default: 30 minutes
#ifndef CHECK_DELAY_MS
#define CHECK_DELAY_MS 30 * 60 * 1000
#endif

#define OBJECT_FILE 0
#define OBJECT_REGISTRY 1

#define BUF_LEN 256


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

        // If no stop event, check only once
        if (stopEvent == INVALID_HANDLE_VALUE) return;

        // Sleep for Check Delay while listening for stop signal
        res = WaitForSingleObject(stopEvent, CHECK_DELAY_MS);
        if (res != WAIT_TIMEOUT) return;
    }
}


#define ReportObjErrorAndRet() \
    do { \
        snprintf(buf, BUF_LEN-1, "Verification for object '%s': Failed (bad JSON)", szObjectName);   \
        SvcReportEvent(EVENTLOG_ERROR_TYPE, buf);                                           \
        return;                                                                       \
    } while (0)


void VerifyObject(cJSON* jsonObject) {
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
    HANDLE hBaseHnd;
    DWORD dwBackslashIndex;
    HKEY hkBaseKey, hkRoot;
    int res;

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

    // Check presense and get base handle
    switch (dwType) {

        case OBJECT_FILE:
            hBaseHnd = CreateFile(szPath,
                               GENERIC_READ,
                               FILE_SHARE_READ,
                               0,
                               OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS,
                               NULL);

            if (hBaseHnd == INVALID_HANDLE_VALUE) {
                res = GetLastError();
                if (res == ERROR_FILE_NOT_FOUND)
                    snprintf(buf, BUF_LEN-1, "Object '%s': Missing", szObjectName);
                else snprintf(buf, BUF_LEN-1, "Object '%s': Failed to open (%d)", szObjectName, res);
                SvcReportEvent(EVENTLOG_ERROR_TYPE, buf);
                return;
            }
            VerifyNodeFile(jsonRootNode, hBaseHnd);
            CloseHandle(hBaseHnd);
            break;

        case OBJECT_REGISTRY:
            hkRoot = ParseRootHKEY(szPath);
            if (hkRoot == INVALID_HANDLE_VALUE) {
                snprintf(buf, BUF_LEN-1, "Object '%s': Invalid root HKEY");
                SvcReportEvent(EVENTLOG_ERROR_TYPE, buf);
                return;
            }
            // safe, since hkRoot found '\\'
            dwBackslashIndex = strchr(szPath, '\\') - szPath;
            res = RegOpenKeyEx(hkRoot, szPath + dwBackslashIndex + 1, 0, KEY_READ, &hkBaseKey);

            if (res != ERROR_SUCCESS) {
                if (res == ERROR_FILE_NOT_FOUND)
                    snprintf(buf, BUF_LEN-1, "Object '%s': Missing", szObjectName);
                else snprintf(buf, BUF_LEN-1, "Object '%s': Failed to open (%d)", szObjectName, res);
                SvcReportEvent(EVENTLOG_ERROR_TYPE, buf);
                return;
            }
            VerifyNodeReg(jsonRootNode, hkBaseKey);
            RegCloseKey(hkBaseKey);
            break;

        default:
            snprintf(buf, BUF_LEN-1, "Object '%s': Unknown object type", szObjectName);
            SvcReportEvent(EVENTLOG_ERROR_TYPE, buf);
            return;
    }
    snprintf(buf, BUF_LEN-1, "Object '%s': Verification complete", szObjectName);
    SvcReportEvent(EVENTLOG_INFORMATION_TYPE, buf);
}


void VerifyNodeFile(cJSON* jsonNode, HANDLE hBase) {
    /**
     * @brief Verify HashNode against actual sub-folder or file
     *
     * @details go DFS
     *  for leaves:
     *      - check presense
     *      - check hash
     *      - report on mismatch
     *  for nodes:
     *      - check presense
     *      - for each subnode:
     *          recursive call
     *      - for each item in actual object:
     *          add name and type to hash
     *      - report on mismatch
     */

    TCHAR buf[BUF_LEN];
    TCHAR szPath[MAX_PATH];

    HANDLE hCurrent = hBase;
    DWORD res;
    BOOL isDirectory;
    BOOL hasSlaves;

    // Get name
    LPTSTR szName = NULL;
    cJSON* jsonName = cJSON_GetObjectItem(jsonNode, "name");
    if (jsonName && cJSON_IsString(jsonName))
        szName = cJSON_GetStringValue(jsonName);

    // Get path (for logging)
    res = GetFinalPathNameByHandle(hBase, szPath, MAX_PATH-1, 0);
    if (res <= 0) strcpy(szPath, "<unknown>");

    // Get slaves in OL
    cJSON* jsonSlaves = cJSON_GetObjectItem(jsonNode, "slaves");
    hasSlaves = (jsonSlaves && cJSON_IsArray(jsonSlaves));

    // Name is set, check presense and actual type
    if (szName) {
        snprintf(szPath + _tcslen(szPath), MAX_PATH - _tcslen(szPath), "\\%s", szName);

        // Check presense, assuming hBase is valid
        hCurrent = CreateFile(szPath,
                               GENERIC_READ,
                               FILE_SHARE_READ,
                               0,
                               OPEN_EXISTING,
                               FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_SEQUENTIAL_SCAN,
                               NULL);

        if (hCurrent == INVALID_HANDLE_VALUE) {
            res = GetLastError();
            if (res == ERROR_FILE_NOT_FOUND)
                snprintf(buf, BUF_LEN - 1, "File '%s': Missing", szPath);
            else snprintf(buf, BUF_LEN - 1, "File '%s': Failed to open (%lu)", szPath, res);
            SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
            return;
        }

        isDirectory = (GetFileAttributes(szPath) & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;
        if (isDirectory != hasSlaves) {
            if (!isDirectory) snprintf(buf, BUF_LEN-1, "Folder '%s': Expected directory, got file", szPath);
            else              snprintf(buf, BUF_LEN-1, "File '%s': Expected file, got directory", szPath);
            SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
            return;
        }
    }
    else hCurrent = hBase;  // szName not set -> it is root

    // Check slaves (recursive)
    if (hasSlaves)
        for (int i = 0; i < cJSON_GetArraySize(jsonSlaves); i++)
            VerifyNodeFile(cJSON_GetArrayItem(jsonSlaves, i), hCurrent);

    // Verify hash (if set)
    cJSON* jsonHash = cJSON_GetObjectItem(jsonNode, "hash");
    if (jsonHash && cJSON_IsString(jsonHash)) {
        LPTSTR szExpectedHash = cJSON_GetStringValue(jsonHash);
        TCHAR szActualHash[MD5LEN*2 + 1] = {0};

        // File: compute and compare file hash
        if (!isDirectory) {
            res = MD5_FileHashDigest(hCurrent, szActualHash);
            if (res != ERROR_SUCCESS) {
                snprintf(buf, BUF_LEN-1, "File '%s': Could not compute hash", szPath);
                SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
                if (hCurrent != hBase) CloseHandle(hCurrent);
                return;
            }
            if (0 != strncmp(szExpectedHash, szActualHash, MD5LEN * 2)) {
                snprintf(buf, BUF_LEN-1, "File '%s': File was modified (hash mismatch)", szPath);
                SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
                if (hCurrent != hBase) CloseHandle(hCurrent);
                return;
            }
        }
        // TODO hash for directory?
    }
    if (hCurrent != hBase) CloseHandle(hCurrent);

    // Warning: massive spam to event log if checking many files
#ifdef REPORT_SUCCESSFUL_CHECKS
    snprintf(buf, BUF_LEN-1, "Path '%s': OK", szPath);
    SvcReportEvent(EVENTLOG_INFORMATION_TYPE, buf);
#endif
}


void VerifyNodeReg(cJSON* jsonNode, HKEY hBase) {
    /**
     * @brief Verify HashNode against actual sub-key or value
     *
     * @details go DFS
     *  for leaves:
     *      - check presense
     *      - check hash
     *      - report on mismatch
     *  for nodes:
     *      - check presense
     *      - for each subnode:
     *          recursive call
     *      - for each item in actual object:
     *          add name and type to hash
     *      - report on mismatch
     */

    TCHAR buf[BUF_LEN];
    LPBYTE pbRegBuf;

    HKEY hCurrent = hBase;
    DWORD res, dwType, dwSize;
    BOOL hasSlaves;

    // Get name
    LPTSTR szName = NULL;
    cJSON* jsonName = cJSON_GetObjectItem(jsonNode, "name");
    if (jsonName && cJSON_IsString(jsonName))
        szName = cJSON_GetStringValue(jsonName);

    // Get slaves in OL
    cJSON* jsonSlaves = cJSON_GetObjectItem(jsonNode, "slaves");
    hasSlaves = (jsonSlaves && cJSON_IsArray(jsonSlaves));

    // name set, check presense
    if (szName) {

        if (hasSlaves)
            res = RegOpenKeyEx(hBase, szName, 0, KEY_READ, &hCurrent);
        else
            res = RegQueryValueEx(hBase, szName, NULL, &dwType, NULL, &dwSize);

        if (res != ERROR_SUCCESS && res != ERROR_INSUFFICIENT_BUFFER) {
            if (res == ERROR_FILE_NOT_FOUND)
                snprintf(buf, BUF_LEN-1, "Key '%s': Missing", szName);
            else snprintf(buf, BUF_LEN-1, "Key '%s': Failed to open (%lu)", szName, res);
            SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
            return;
        }
    }
    else hCurrent = hBase;

    // Check slaves (recursive)
    if (hasSlaves)
        for (int i = 0; i < cJSON_GetArraySize(jsonSlaves); i++)
            VerifyNodeReg(cJSON_GetArrayItem(jsonSlaves, i), hCurrent);

    // Verify hash (if set)
    cJSON* jsonHash = cJSON_GetObjectItem(jsonNode, "hash");
    if (jsonHash && cJSON_IsString(jsonHash)) {
        LPTSTR szExpectedHash = cJSON_GetStringValue(jsonHash);
        TCHAR szActualHash[MD5LEN*2 + 1] = {0};

        // Value: compute hash(dwType | Value)
        if (!hasSlaves) {
            pbRegBuf = malloc(dwSize * sizeof(BYTE) + sizeof(DWORD));
            if (!pbRegBuf) return;

            // Write Type (DWORD)
            memcpy(pbRegBuf, &dwType, sizeof(DWORD));

            // Get value from reg
            res = RegQueryValueEx(hBase, szName, NULL, NULL, pbRegBuf+sizeof(DWORD), &dwSize);
            if (res == ERROR_SUCCESS)
                res = MD5_MemHashDigest(pbRegBuf, dwSize + sizeof(DWORD), szActualHash);

            if (res != ERROR_SUCCESS) {
                snprintf(buf, BUF_LEN-1, "Value '%s': Could not compute hash (%lu)", szName, res);
                SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
                free(pbRegBuf);
                return;
            }

            if (0 != strncmp(szExpectedHash, szActualHash, MD5LEN * 2)) {
                snprintf(buf, BUF_LEN-1, "Value '%s': Value was modified (hash mismatch)", szName);
                SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
                free(pbRegBuf);
                return;
            }

            free(pbRegBuf);
        }
        // TODO hash for key?
    }
    if (hCurrent != hBase) RegCloseKey(hCurrent);

    // Shouldn't really spam event log, but who knows...
    if (szName) {
        snprintf(buf, BUF_LEN-1, "Key '%s': OK", szName);
        SvcReportEvent(EVENTLOG_INFORMATION_TYPE, buf);
    }
}
