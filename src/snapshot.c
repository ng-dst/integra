#include <stdio.h>
#include <tchar.h>
#include "md5.h"
#include "utils.h"
#include "snapshot.h"


#define BUF_LEN 256


cJSON* SnapshotObject(DWORD dwType, LPCTSTR szObjectName, LPCTSTR szPath) {
    /**
     * @brief Create HashTree of object
     *
     * @details Given object as Folder or Key, make a JSON:
     *      string  object_name
     *      DWORD   type
     *      string  path
     *      cJSON   root
     *
     *  Where root is the root HashNode of HashTree, given as cJSON:
     *      string  name    -(for root)
     *      string  hash    -(skip hash check?)
     *      [cJSON] slaves
     */

    TCHAR szFinalPath[MAX_PATH];
    HANDLE hBaseHnd;
    HKEY hkBaseKey, hkRoot;
    DWORD dwBackslashIndex;
    cJSON* jsonRootNode;
    int res;

    printf("Making snapshot of object '%s'...\n", szObjectName);

    cJSON* jsonObject = cJSON_CreateObject();
    if (!jsonObject) return NULL;

    // Set basic properties
    cJSON_AddStringToObject(jsonObject, "object_name", szObjectName);
    cJSON_AddNumberToObject(jsonObject, "type", dwType);

    // Check presence and get base handle, proceed to node snapshot
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

            // Set actual absolute path
            GetFinalPathNameByHandle(hBaseHnd, szFinalPath, MAX_PATH, VOLUME_NAME_DOS);
            cJSON_AddStringToObject(jsonObject, "path", szFinalPath);

            // Proceed to node backup
            jsonRootNode = SnapshotNodeFile(hBaseHnd, NULL);
            CloseHandle(hBaseHnd);
            if (!jsonRootNode) { cJSON_Delete(jsonObject); return NULL; }

            // OK return snapshot of object
            cJSON_AddItemToObject(jsonObject, "root", jsonRootNode);
            return jsonObject;

        case OBJECT_REGISTRY:
            // Get root HKEY from path. A definition like HKEY_CLASSES_ROOT is returned
            hkRoot = ParseRootHKEY(szPath);
            if (hkRoot == INVALID_HANDLE_VALUE) {
                printf("Invalid root HKEY. Must be of format HKEY\\Path\\Key (ex. HKEY_USERS\\User\\Key)\n");
                cJSON_Delete(jsonObject);
                return NULL;
            }
            // Safe, since hkRoot found '\\'
            dwBackslashIndex = strchr(szPath, '\\') - szPath;
            res = RegOpenKeyEx(hkRoot, szPath + dwBackslashIndex + 1, 0, KEY_READ, &hkBaseKey);

            if (res != ERROR_SUCCESS) {
                if (res == ERROR_FILE_NOT_FOUND)
                    printf("Object '%s': Missing\n", szObjectName);
                else printf("Object '%s': Failed to open (%d)\n", szObjectName, res);

                cJSON_Delete(jsonObject);
                return NULL;
            }

            // Set base path
            cJSON_AddStringToObject(jsonObject, "path", szPath);

            // Proceed to node snapshot
            jsonRootNode = SnapshotNodeReg(hkBaseKey, NULL, TRUE);
            RegCloseKey(hkBaseKey);
            if (!jsonRootNode) { cJSON_Delete(jsonObject); return NULL; }

            // OK return snapshot of object
            cJSON_AddItemToObject(jsonObject, "root", jsonRootNode);
            return jsonObject;

        default:
            // wat?
            printf("Object '%s': Unknown object type\n", szObjectName);
            return NULL;
    }
}


cJSON* SnapshotNodeFile(HANDLE hBase, LPCTSTR szName) {
    /**
     * @brief Verify HashNode against actual sub-folder or file
     *
     * @details go DFS
     *  for files:
     *      - check presence
     *      - compute hash
     *
     *  for directories:
     *      - check presence
     *      - for each item:
     *          recursive call
     *
     * -------------------------------------------------------------------------------------- *
     *    Hash for key is computed as:
     *        MD5( file contents )
     *
     *    using MD5_FileHashDigest()
     *
     * -------------------------------------------------------------------------------------- *
     *    Hash for directory is NOT computed (out-of-scope and new files are ignored)
     *
     * -------------------------------------------------------------------------------------- *
     */

    TCHAR szPath[MAX_PATH];

    HANDLE hCurrent;
    DWORD res;
    BOOL isDirectory;

    cJSON* jsonNode = cJSON_CreateObject();
    if (!jsonNode) return NULL;

    // Get path by base handle:  szPath
    res = GetFinalPathNameByHandle(hBase, szPath, MAX_PATH-1, VOLUME_NAME_DOS);
    if (res <= 0) strcpy(szPath, "<unknown>");

    // Set name (if present)
    if (szName) cJSON_AddStringToObject(jsonNode, "name", szName);
    else cJSON_AddNullToObject(jsonNode, "name");

    // If name is set, check presence and obtain handle:  hCurrent
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
                printf("File '%s': Missing\n", szPath);
            else printf("File '%s': Failed to open (%lu)\n", szPath, res);
            cJSON_Delete(jsonNode);
            return NULL;
        }
    }
    else hCurrent = hBase;  // szName not set -> it is root, use hBase instead

    isDirectory = (GetFileAttributes(szPath) & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY;

    // Directory: add slaves (recursive)
    if (isDirectory) {

        cJSON* jsonSlavesArr = cJSON_AddArrayToObject(jsonNode, "slaves");

        // Search for files and sub-folders. To do this, append '\*' to path:  C:\path\*
        snprintf(szPath + _tcslen(szPath), MAX_PATH - _tcslen(szPath), "\\*");

        WIN32_FIND_DATA wfd;
        HANDLE hFind = FindFirstFile(szPath, &wfd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                if (!(wfd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) &&
                                      0 != _tcscmp(_T("."), wfd.cFileName) &&
                                      0 != _tcscmp(_T(".."), wfd.cFileName)) {
                    // Recursive call
                    cJSON *jsonSlave = SnapshotNodeFile(hCurrent, wfd.cFileName);

                    // Add to slaves list for current node
                    if (jsonSlave) cJSON_AddItemToArray(jsonSlavesArr, jsonSlave);
                }
            } while (FindNextFile(hFind, &wfd));
            FindClose(hFind);
        }
    }
    else {  // File: compute hash
        /*
         *  Hash for file is computed with  MD5_FileHashDigest()
         *  If failed, store NULL hash: we mark presence of file but don't snapshot its contents
         */
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
     *  for values:
     *      - check presence
     *      - calculate hash
     *  for keys:
     *      - check presence
     *      - for each sub-key:
     *          recursive call
     *          add name to hash
     *      - for each value in key:
     *          recursive call
     *          add name to hash
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

    HKEY hCurrent = hBase;
    DWORD res, dwType, dwSize;

    cJSON* jsonNode = cJSON_CreateObject();
    if (!jsonNode) return NULL;

    // set name
    if (szName) cJSON_AddStringToObject(jsonNode, "name", szName);
    else cJSON_AddNullToObject(jsonNode, "name");

    // Name is set, check presence and compute hash
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
    }  // if szName not set -> it is root node, use hBase instead

    TCHAR szActualHash[MD5LEN*2 + 1] = {0};

    if (isKey) {
        // compute hash for key (see implementation)
        MD5_RegKeyHashDigest(hCurrent, szActualHash);
        cJSON_AddStringToObject(jsonNode, "hash", szActualHash);

        cJSON* jsonSlavesArr = cJSON_AddArrayToObject(jsonNode, "slaves");

        // Iterate over sub-keys
        TCHAR szSlaveName[MAX_PATH];
        DWORD dwIndex = 0;
        while (ERROR_SUCCESS == RegEnumKey(hCurrent, dwIndex, szSlaveName, MAX_PATH)) {
            // Recursive call. Add to slaves list of current node
            cJSON* jsonSlave = SnapshotNodeReg(hCurrent, szSlaveName, TRUE);
            if (jsonSlave) cJSON_AddItemToArray(jsonSlavesArr, jsonSlave);
            dwIndex++;
        }

        // Iterate over values
        dwSize = MAX_PATH;
        dwIndex = 0;
        while (ERROR_SUCCESS == RegEnumValue(hCurrent, dwIndex, szSlaveName, &dwSize, NULL, NULL, NULL, NULL)) {
            // Recursion, again. Add to slaves list, again
            cJSON* jsonSlave = SnapshotNodeReg(hCurrent, szSlaveName, FALSE);
            if (jsonSlave) cJSON_AddItemToArray(jsonSlavesArr, jsonSlave);
            dwIndex++;
        }

        if (hCurrent != hBase) RegCloseKey(hCurrent);
    }
    else {  // !isKey
        // Value: compute MD5( dwType | rbValue)  (see implementation)
        if (ERROR_SUCCESS == MD5_RegValueHashDigest(hCurrent, szName, szActualHash))
            cJSON_AddStringToObject(jsonNode, "hash", szActualHash);
        else {
            printf("Value '%s': failed to compute hash\n", szName);
            cJSON_AddNullToObject(jsonNode, "hash");
        }
    }
    
#ifdef REPORT_SUCCESSFUL_CHECKS
    if (szName) printf("Snapshot of path '%s': Done\n", szName);
#endif
    return jsonNode;
}


