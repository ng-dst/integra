#include <stdio.h>
#include <tchar.h>
#include "md5.h"
#include "utils.h"
#include "snapshot.h"


#define OBJECT_FILE 0
#define OBJECT_REGISTRY 1

#define BUF_LEN 256


cJSON* SnapshotObject(DWORD dwType, LPCTSTR szObjectName, LPCTSTR szPath) {
    /**
     * @brief Create HashTree of object
     *
     * @details Given object as Folder or Key, make a JSON:
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

    HANDLE hBaseHnd;
    HKEY hkBaseKey, hkRoot;
    DWORD dwBackslashIndex;
    LPTSTR lpIndex;
    int res;
    cJSON* jsonRootNode;

    printf("Making snapshot of object '%s'...\n", szObjectName);

    cJSON* jsonObject = cJSON_CreateObject();
    if (!jsonObject) return NULL;

    cJSON_AddStringToObject(jsonObject, "object_name", szObjectName);
    cJSON_AddNumberToObject(jsonObject, "type", dwType);
    cJSON_AddStringToObject(jsonObject, "path", szPath);

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
                    printf("Object '%s': Missing\n", szObjectName);
                else printf("Object '%s': Failed to open (%d)\n", szObjectName, res);
                cJSON_Delete(jsonObject);
                return NULL;
            }

            jsonRootNode = SnapshotNodeFile(hBaseHnd, NULL);
            CloseHandle(hBaseHnd);
            if (!jsonRootNode) {
                cJSON_Delete(jsonObject);
                return NULL;
            }
            cJSON_AddItemToObject(jsonObject, "root", jsonRootNode);
            return jsonObject;

        case OBJECT_REGISTRY:
            hkRoot = ParseRootHKEY(szPath);
            if (hkRoot == INVALID_HANDLE_VALUE) {
                printf("Invalid root HKEY. Must be of format HKEY\\Path\\Key (ex. HKEY_USERS\\User\\Key)\n");
                cJSON_Delete(jsonObject);
                return NULL;
            }
            // safe, since hkRoot found '\\'
            dwBackslashIndex = strchr(szPath, '\\') - szPath;
            res = RegOpenKeyEx(hkRoot, szPath + dwBackslashIndex + 1, 0, KEY_READ, &hkBaseKey);

            if (res != ERROR_SUCCESS) {
                if (res == ERROR_FILE_NOT_FOUND)
                    printf("Object '%s': Missing\n", szObjectName);
                else printf("Object '%s': Failed to open (%d)\n", szObjectName, res);
                cJSON_Delete(jsonObject);
                return NULL;
            }
            jsonRootNode = SnapshotNodeReg(hkBaseKey, NULL, TRUE);
            RegCloseKey(hkBaseKey);
            if (!jsonRootNode) {
                cJSON_Delete(jsonObject);
                return NULL;
            }
            cJSON_AddItemToObject(jsonObject, "root", jsonRootNode);
            return jsonObject;

        default:
            printf("Object '%s': Unknown object type\n", szObjectName);
            return NULL;
    }
}


cJSON* SnapshotNodeFile(HANDLE hBase, LPCTSTR szName) {
    /**
     * @brief Verify HashNode against actual sub-folder or file
     *
     * @details go DFS
     *  for leaves:
     *      - check presense
     *      - compute hash
     *  for nodes:
     *      - check presense
     *      - for each subnode:
     *          recursive call
     *      - for each item in actual object:
     *          add name and type to hash
     */

    TCHAR szPath[MAX_PATH];

    HANDLE hCurrent = hBase;
    DWORD res;
    BOOL isDirectory;

    cJSON* jsonNode = cJSON_CreateObject();
    if (!jsonNode) return NULL;

    // Get path (for logging)
    res = GetFinalPathNameByHandle(hBase, szPath, MAX_PATH-1, 0);
    if (res <= 0) strcpy(szPath, "<unknown>");

    // set name
    if (szName) cJSON_AddStringToObject(jsonNode, "name", szName);
    else cJSON_AddNullToObject(jsonNode, "name");

    // Name is set, check presense and compute hash
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
                printf("File '%s': Missing\n", szPath);
            else printf("File '%s': Failed to open (%lu)\n", szPath, res);
            cJSON_Delete(jsonNode);
            return NULL;
        }
    }
    else hCurrent = hBase;  // szName not set -> it is root

    isDirectory = (GetFileAttributes(szPath) & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;

    // add slaves (recursive)
    if (isDirectory) {
        snprintf(szPath + _tcslen(szPath), MAX_PATH - _tcslen(szPath), "\\*");

        cJSON* jsonSlavesArr = cJSON_AddArrayToObject(jsonNode, "slaves");

        // Iterate over files in folder
        WIN32_FIND_DATA wfd;
        HANDLE hFind = FindFirstFile(szPath, &wfd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                                      0 != _tcscmp(_T("."), wfd.cFileName) &&
                                      0 != _tcscmp(_T(".."), wfd.cFileName)) {
                    // Recursive call
                    cJSON *jsonSlave = SnapshotNodeFile(hCurrent, wfd.cFileName);
                    if (jsonSlave) cJSON_AddItemToArray(jsonSlavesArr, jsonSlave);
                    // TODO directory hash
                }
            } while (FindNextFile(hFind, &wfd));
            FindClose(hFind);
        }
    }
    else {  // !isDirectory
        TCHAR szActualHash[MD5LEN*2 + 1] = {0};
        res = MD5_FileHashDigest(hCurrent, szActualHash);
        if (res != ERROR_SUCCESS) {
            printf("File '%s': Could not compute hash\n", szPath);
            if (hCurrent != hBase) CloseHandle(hCurrent);
            cJSON_AddNullToObject(jsonNode, "hash");
        }
        else cJSON_AddStringToObject(jsonNode, "hash", szActualHash);
    }

    if (hCurrent != hBase) CloseHandle(hCurrent);
#ifdef REPORT_SUCCESSFUL_CHECKS
    printf("Snapshot of path '%s': Done\n", szPath);
#endif
    return jsonNode;
}


cJSON* SnapshotNodeReg(HKEY hBase, LPCTSTR szName, BOOL isKey) {
    /**
     * @brief Make HashNode of sub-key or value
     *
     * @details go DFS
     *  for leaves:
     *      - check presense
     *      - calculate hash
     *
     *  for nodes:
     *      - check presense
     *      - for each subnode:
     *          recursive call
     *      - for each item in actual object:
     *          add name and type to hash
     */

    HKEY hCurrent = hBase;
    DWORD res, dwType, dwSize;

    cJSON* jsonNode = cJSON_CreateObject();
    if (!jsonNode) return NULL;

    // set name
    if (szName) cJSON_AddStringToObject(jsonNode, "name", szName);
    else cJSON_AddNullToObject(jsonNode, "name");

    // Name is set, check presense and compute hash
    if (szName) {
        // For keys: open registry key
        // For values: read value's type and size
        if (isKey) res = RegOpenKeyEx(hBase, szName, 0, KEY_READ, &hCurrent);
        else       res = RegQueryValueEx(hBase, szName, NULL, &dwType, NULL, &dwSize);

        if (res != ERROR_SUCCESS && res != ERROR_INSUFFICIENT_BUFFER) {
            if (res == ERROR_FILE_NOT_FOUND)
                 printf("Key '%s': Missing\n", szName);
            else printf("Key '%s': Failed to open (%lu)\n", szName, res);
            return NULL;
        }
    }
    else hCurrent = hBase;  // szName not set -> it is root

    // add slaves (recursive)
    if (isKey) {
        cJSON* jsonSlavesArr = cJSON_AddArrayToObject(jsonNode, "slaves");

        // Iterate over sub-keys
        DWORD dwIndex = 0;
        TCHAR szSlaveName[MAX_PATH];

        while (ERROR_SUCCESS == RegEnumKey(hCurrent, dwIndex, szSlaveName, MAX_PATH)) {
            // Recursive call
            cJSON* jsonSlave = SnapshotNodeReg(hCurrent, szSlaveName, TRUE);
            if (jsonSlave) cJSON_AddItemToArray(jsonSlavesArr, jsonSlave);
            dwIndex++;
        }

        dwIndex = 0;
        dwSize = MAX_PATH;
        while (ERROR_SUCCESS == RegEnumValue(hCurrent, dwIndex, szSlaveName, &dwSize, NULL, NULL, NULL, NULL)) {
            // Recursion, again
            cJSON* jsonSlave = SnapshotNodeReg(hCurrent, szSlaveName, FALSE);
            if (jsonSlave) cJSON_AddItemToArray(jsonSlavesArr, jsonSlave);
            dwIndex++;
        }
        if (hCurrent != hBase) RegCloseKey(hCurrent);

        // TODO key hash?
    }
    else {  // !isKey
        TCHAR szActualHash[MD5LEN*2 + 1] = {0};
        BYTE* pbRegBuf = malloc(dwSize * sizeof(BYTE) + sizeof(DWORD));
        if (!pbRegBuf) return jsonNode;

        // Write Type (DWORD)
        memcpy(pbRegBuf, &dwType, sizeof(DWORD));

        // Get value from reg
        res = RegQueryValueEx(hBase, szName, NULL, NULL, pbRegBuf+sizeof(DWORD), &dwSize);
        if (res == ERROR_SUCCESS)
            res = MD5_MemHashDigest(pbRegBuf, dwSize + sizeof(DWORD), szActualHash);

        if (res != ERROR_SUCCESS) {
            printf("Value '%s': Could not compute hash (%lu)\n", szName, res);
            free(pbRegBuf);
            return jsonNode;
        }
        else cJSON_AddStringToObject(jsonNode, "hash", szActualHash);
    }

    printf("Snapshot of path '%s': Done\n", szName);
    return jsonNode;

}


