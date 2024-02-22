#include <stdio.h>
#include <tchar.h>
#include "md5.h"
#include "cfg.h"
#include "event.h"
#include "utils.h"
#include "integra.h"


#define BUF_LEN 256

CRITICAL_SECTION csVerification;


void NotificationLoopThread(HANDLE stopEvent) {
    /**
     * @brief Thread for registering and processing Change Notifications
     */

    // Read path to OL from registry
    LPTSTR szOlPath = GetOLFilePath();
    if (!szOlPath)
        return;

    // Read Object List
    cJSON* jsonObjectList = ReadJSON(szOlPath);
    if (!jsonObjectList || !cJSON_IsArray(jsonObjectList)) {
        TCHAR buf[MAX_PATH + 64];
        snprintf(buf, MAX_PATH + 63, "Could not load Object List from '%s'", szOlPath);
        SvcReportEvent(EVENTLOG_ERROR_TYPE, buf);
        return;
    }

    // Get objects as JSON array -> paths -> handles array
    DWORD dwNumObjects = cJSON_GetArraySize(jsonObjectList);
    HANDLE* lpChangeHandles = calloc(dwNumObjects + 1, sizeof(HANDLE));
    if (!lpChangeHandles) { cJSON_Delete(jsonObjectList); return; }

    for (int i = 0; i < dwNumObjects; i++) {
        // Workaround for error on invalid handles
        lpChangeHandles[i] = stopEvent;

        // Parse JSON objects to get paths
        cJSON* jsonArrItem = cJSON_GetArrayItem(jsonObjectList, i);
        cJSON* jsonPath = cJSON_GetObjectItem(jsonArrItem, "path");
        if (!cJSON_IsString(jsonPath)) continue;
        LPCTSTR szPath = cJSON_GetStringValue(jsonPath);

        cJSON* jsonType = cJSON_GetObjectItem(jsonArrItem, "type");
        if (!cJSON_IsNumber(jsonType)) continue;
        DWORD dwType = (DWORD) cJSON_GetNumberValue(jsonType);

        // Not using Change Notification for registry
        if (dwType != OBJECT_FILE)
            continue;

        // Register for notifications
        lpChangeHandles[i] = FindFirstChangeNotification(szPath, TRUE, FILE_NOTIFY_CHANGE_LAST_WRITE |
                                                                       FILE_NOTIFY_CHANGE_DIR_NAME |
                                                                       FILE_NOTIFY_CHANGE_FILE_NAME |
                                                                       FILE_NOTIFY_CHANGE_CREATION);
        TCHAR buf[MAX_PATH + BUF_LEN];
        if (lpChangeHandles[i] == INVALID_HANDLE_VALUE || lpChangeHandles[i] == NULL) {
            // Error while registering
            snprintf(buf, MAX_PATH + BUF_LEN - 1, "Cannot register for change notification for '%s'", szPath);
            SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
            lpChangeHandles[i] = stopEvent;
        }
        else {
            // registration OK
            snprintf(buf, MAX_PATH + BUF_LEN - 1, "Registered for notifications: '%s'", szPath);
            SvcReportEvent(EVENTLOG_INFORMATION_TYPE, buf);
        }
    }
    // Append stop event in order to abort monitoring
    lpChangeHandles[dwNumObjects] = stopEvent;

    while (TRUE) {
        // Wait for handles and verify corresponding objects
        DWORD dwWaitStatus = WaitForMultipleObjects(dwNumObjects + 1, lpChangeHandles, FALSE, INFINITE);
        DWORD dwObjIndex = dwWaitStatus - WAIT_OBJECT_0;

        TCHAR buf[BUF_LEN];
        if (dwObjIndex > dwNumObjects) {
            // error?
            snprintf(buf, BUF_LEN - 1, "Error waiting for Change Notification (%lu)", GetLastError());
            SvcReportEvent(EVENTLOG_ERROR_TYPE, buf);
        }
        if (lpChangeHandles[dwObjIndex] != stopEvent) {
            // object change detected
            snprintf(buf, BUF_LEN - 1, "Changes detected at object #%lu. Awaiting verification.", dwObjIndex);
            SvcReportEvent(EVENTLOG_INFORMATION_TYPE, buf);
            cJSON* jsonArrItem = cJSON_GetArrayItem(jsonObjectList, (int) dwObjIndex);
            EnterCriticalSection(&csVerification);
            VerifyObject(jsonArrItem);
            LeaveCriticalSection(&csVerification);
            if (FindNextChangeNotification(lpChangeHandles[dwObjIndex]) == FALSE) {
                SvcReportEvent(EVENTLOG_WARNING_TYPE, "FindNextChangeNotification failed. Monitoring for the object is on timer now.");
                lpChangeHandles[dwObjIndex] = stopEvent;
            }
        } else {
            // stop event
            for (int i = 0; i < dwNumObjects; i++)
                if (lpChangeHandles[i] != INVALID_HANDLE_VALUE)
                    FindCloseChangeNotification(lpChangeHandles[i]);
            cJSON_Delete(jsonObjectList);
            return;
        }
    }
}


void ServiceLoop(HANDLE stopEvent) {
    /**
     * @brief Main loop for service. Truly main.
     *
     * @details sleep for delay, then perform hash check based on object list (path in registry, cfg.h)
     *  Can be run manually (outside of service): call with stopEvent = INTEGRA_CHECK_ONCE
     */

    InitializeCriticalSection(&csVerification);

    // Read interval from registry
    DWORD dwIntervalMs = GetCheckInterval();
    if (!dwIntervalMs) dwIntervalMs = DEFAULT_CHECK_INTERVAL_MS;
    DWORD res, dwNumObjects;
    HANDLE hCnThread = INVALID_HANDLE_VALUE;

    // Read path to OL from registry
    LPTSTR szOlPath = GetOLFilePath();
    if (!szOlPath) {
        SvcReportEvent(EVENTLOG_ERROR_TYPE, "Object List file is not set. Please run (as admin):\n"
               "\tintegra list path <path>"
               "\n  where <path> is absolute path to store Object List at (ex. C:\\path\\objects.json)\n");
        return;
    }

    // Runs as service, report params and create Change Notifications thread
    if (stopEvent != INTEGRA_CHECK_ONCE) {
        TCHAR buf[BUF_LEN];
        snprintf(buf, BUF_LEN-1, "Service is running. Interval: %lu, List: %s", dwIntervalMs, szOlPath);
        SvcReportEvent(EVENTLOG_INFORMATION_TYPE, buf);
#ifndef CHANGE_NOTIFICATION_DISABLE
        // Run Change Notification thread
        hCnThread = CreateThread(NULL, 0, (LPVOID) NotificationLoopThread, stopEvent, 0, NULL);
        if (hCnThread == INVALID_HANDLE_VALUE)
            SvcReportEvent(EVENTLOG_ERROR_TYPE, "Could not start Change Notification thread");
#endif
    }

    while (TRUE) {
        // Read Object List
        cJSON* jsonObjectList = ReadJSON(szOlPath);
        if (jsonObjectList && cJSON_IsArray(jsonObjectList)) {
            // Verify objects
            dwNumObjects = cJSON_GetArraySize(jsonObjectList);
            EnterCriticalSection(&csVerification);
            for (int i = 0; i < dwNumObjects; i++)
                VerifyObject(cJSON_GetArrayItem(jsonObjectList, i));
            LeaveCriticalSection(&csVerification);
        }
        else SvcReportEvent(EVENTLOG_ERROR_TYPE, "Could not read JSON from OL path");

        if (jsonObjectList) cJSON_Delete(jsonObjectList);

        // If no stop event, check only once
        if (stopEvent == INTEGRA_CHECK_ONCE) {
            DeleteCriticalSection(&csVerification);
            return;
        }

        // Sleep for Check Delay while listening for stop signal
        res = WaitForSingleObject(stopEvent, dwIntervalMs);
        if (res != WAIT_TIMEOUT) {
            if (hCnThread != INVALID_HANDLE_VALUE) {
                DWORD dwWaitStatus = WaitForSingleObject(hCnThread, 3000);
                if (dwWaitStatus == WAIT_TIMEOUT) TerminateThread(hCnThread, ERROR_TIMEOUT);
                CloseHandle(hCnThread);
            }
            DeleteCriticalSection(&csVerification);
            return;
        }
    }
}

#define ReportObjErrorAndRet() \
    do { \
        snprintf(buf, BUF_LEN - 1, "Verification for object '%s': Failed (bad JSON)", szObjectName);   \
        SvcReportEvent(EVENTLOG_ERROR_TYPE, buf);                                           \
        return;                                                                       \
    } while (0)


void VerifyObject(cJSON* jsonObject) {
    /**
     * @brief Verify Hash Tree of object against actual object
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

    snprintf(buf, BUF_LEN-1, "Object '%s': Started verification", szObjectName);
    SvcReportEvent(EVENTLOG_INFORMATION_TYPE, buf);

    cJSON* jsonPath = cJSON_GetObjectItem(jsonObject, "path");
    if (!jsonPath || !cJSON_IsString(jsonPath)) ReportObjErrorAndRet();
    LPTSTR szPath = cJSON_GetStringValue(jsonPath);

    cJSON* jsonType = cJSON_GetObjectItem(jsonObject, "type");
    if (!jsonType || !cJSON_IsNumber(jsonType)) ReportObjErrorAndRet();
    DWORD dwType = (DWORD) cJSON_GetNumberValue(jsonType);

    cJSON* jsonRootNode = cJSON_GetObjectItem(jsonObject, "root");
    if (!jsonRootNode) ReportObjErrorAndRet();

    // Check presence and obtain base handle, proceed to Hash Tree verification
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
            // Parse root HKEY from path to get definition like HKEY_CLASSES_ROOT
            hkRoot = ParseRootHKEY(szPath);
            if (hkRoot == INVALID_HANDLE_VALUE) {
                snprintf(buf, BUF_LEN-1, "Object '%s': Invalid root HKEY");
                SvcReportEvent(EVENTLOG_ERROR_TYPE, buf);
                return;
            }
            // Safe, since hkRoot found '\\'
            dwBackslashIndex = (LPVOID) _tcschr(szPath, '\\') - (LPVOID) szPath;
            res = RegOpenKeyEx(hkRoot, szPath + dwBackslashIndex + 1, 0, KEY_READ, &hkBaseKey);

            if (res != ERROR_SUCCESS) {
                if (res == ERROR_FILE_NOT_FOUND)
                    snprintf(buf, BUF_LEN-1, "Object '%s': Missing", szObjectName);
                else snprintf(buf, BUF_LEN-1, "Object '%s': Failed to open (%d)", szObjectName, res);

                SvcReportEvent(EVENTLOG_ERROR_TYPE, buf);
                return;
            }

            // Proceed to node verification
            VerifyNodeReg(jsonRootNode, hkBaseKey);
            RegCloseKey(hkBaseKey);
            break;

        default:
            // wat? (x2)
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
     *      - check presence
     *      - check hash
     *      - report on mismatch
     *  for nodes:
     *      - check presence
     *      - for each subnode:
     *          recursive call
     *      - for each item in actual object:
     *          add name and type to hash
     *      - report on mismatch
     */

    TCHAR buf[BUF_LEN];
    TCHAR szPath[MAX_PATH];

    HANDLE hCurrent;
    DWORD res;
    BOOL isDirectory;
    BOOL hasSlaves;

    // Get name
    LPTSTR szName = NULL;
    cJSON* jsonName = cJSON_GetObjectItem(jsonNode, "name");
    if (jsonName && cJSON_IsString(jsonName))
        szName = cJSON_GetStringValue(jsonName);

    // Get path
    res = GetFinalPathNameByHandle(hBase, szPath, MAX_PATH-1, VOLUME_NAME_DOS);
    if (res <= 0) _tcscpy(szPath, _T("<unknown>"));

    // Get slaves of node
    cJSON* jsonSlaves = cJSON_GetObjectItem(jsonNode, "slaves");
    hasSlaves = (jsonSlaves && cJSON_IsArray(jsonSlaves));

    // If name is set, check presence and actual type, obtain handle:  hCurrent
    if (szName) {
        snprintf(szPath + _tcslen(szPath), MAX_PATH - _tcslen(szPath), "\\%s", szName);

        // Check presence, assuming hBase is valid
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
            // Node type mismatch. Only directories have slaves list
            if (!isDirectory) snprintf(buf, BUF_LEN-1, "Folder '%s': Expected directory, got file", szPath);
            else              snprintf(buf, BUF_LEN-1, "File '%s': Expected file, got directory", szPath);

            SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
            return;
        }
    }
    else hCurrent = hBase;  // szName not set -> it is root, use hBase instead

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
                snprintf(buf, BUF_LEN-1, "File '%s': Modified (hash mismatch)", szPath);
                SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
                if (hCurrent != hBase) CloseHandle(hCurrent);
                return;
            }
        }
        // Can also perform hash checks for directories
        // But we ignore it for now (allow for new files by default)
        // In future it might be implemented
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
     *      - check presence
     *      - check hash
     *      - report on mismatch
     *
     *  for nodes:
     *      - check presence
     *      - for each subnode:
     *          recursive call
     *      - for each item in actual object:
     *          add name and type to hash
     *      - report on mismatch
     *
     * -------------------------------------------------------------------------------------- *
     *    Hash for value is computed as:
     *
     *      MD5( dwType | rbValue )
     *
     *    where  dwType   -  4-byte DWORD (usually little-endian),
     *           rbValue  -  byte buffer for value,
     *            |       -  concat operation
     *
     * -------------------------------------------------------------------------------------- *
     *    Hash for key is computed as:
     *
     *      MD5( MD5(valueName1)^...^MD5(valueNameN) ^ MD5[MD5(keyName1)^...^MD5(keyNameM)] )
     *
     *    where  valueName1...valueNameN  -  values in key
     *           keyName1...keyNameM      -  sub-keys contained in key
     *            ^                       -  XOR operation
     *
     * -------------------------------------------------------------------------------------- *
     */

    TCHAR buf[BUF_LEN];

    HKEY hCurrent = hBase;
    DWORD res;
    BOOL hasSlaves;

    // Get key / value name
    LPTSTR szName = NULL;
    cJSON* jsonName = cJSON_GetObjectItem(jsonNode, "name");
    if (jsonName && cJSON_IsString(jsonName))
        szName = cJSON_GetStringValue(jsonName);

    // Get slaves list from node
    cJSON* jsonSlaves = cJSON_GetObjectItem(jsonNode, "slaves");
    hasSlaves = (jsonSlaves && cJSON_IsArray(jsonSlaves));

    // name set, check presence and obtain handle:  hCurrent
    if (szName) {

        if (hasSlaves)
            res = RegOpenKeyEx(hBase, szName, 0, KEY_READ, &hCurrent);
        else
            res = RegQueryValueEx(hBase, szName, NULL, NULL, NULL, NULL);

        if (res != ERROR_SUCCESS && res != ERROR_INSUFFICIENT_BUFFER) {
            if (res == ERROR_FILE_NOT_FOUND)
                snprintf(buf, BUF_LEN-1, "Key '%s': Missing", szName);
            else snprintf(buf, BUF_LEN-1, "Key '%s': Failed to open (%lu)", szName, res);

            SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
            return;
        }
    }  // name not set -> it is root, use hBase instead

    // Check slaves (recursive)
    if (hasSlaves)
        for (int i = 0; i < cJSON_GetArraySize(jsonSlaves); i++)
            VerifyNodeReg(cJSON_GetArrayItem(jsonSlaves, i), hCurrent);

    // Verify hash (if set)
    cJSON* jsonHash = cJSON_GetObjectItem(jsonNode, "hash");
    if (jsonHash && cJSON_IsString(jsonHash)) {
        LPTSTR szExpectedHash = cJSON_GetStringValue(jsonHash);
        TCHAR szActualHash[MD5LEN*2 + 1] = {0};

        if (hasSlaves)
            res = MD5_RegKeyHashDigest(hCurrent, szActualHash);
        else
            res = MD5_RegValueHashDigest(hBase, szName, szActualHash);

        if (res != ERROR_SUCCESS) {
            snprintf(buf, BUF_LEN-1, "Key '%s': Could not compute hash", szName ? szName : "\\");
            SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
            if (hCurrent != hBase) RegCloseKey(hCurrent);
            return;
        }

        else if (0 != strncmp(szExpectedHash, szActualHash, MD5LEN * 2)) {
            snprintf(buf, BUF_LEN-1, "Key '%s': Modified (hash mismatch)", szName ? szName : "\\");
            SvcReportEvent(EVENTLOG_WARNING_TYPE, buf);
            if (hCurrent != hBase) RegCloseKey(hCurrent);
            return;
        }

    }
    if (hCurrent != hBase) RegCloseKey(hCurrent);

#ifdef REPORT_SUCCESSFUL_CHECKS
    // Shouldn't really spam event log, but who knows...
    if (szName) {
        snprintf(buf, BUF_LEN-1, "Key '%s': OK", szName);
        SvcReportEvent(EVENTLOG_INFORMATION_TYPE, buf);
    }
#endif
}
