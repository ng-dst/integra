#ifndef INTEGRA_SNAPSHOT_H
#define INTEGRA_SNAPSHOT_H

#include <windows.h>
#include "cjson.h"

cJSON* SnapshotObject(DWORD dwType, LPCTSTR szObjectName, LPCTSTR szPath);
cJSON* SnapshotNodeReg(HKEY hBase, LPCTSTR szName, BOOL isKey);
cJSON* SnapshotNodeFile(HANDLE hBase, LPCTSTR szName);

#endif //INTEGRA_SNAPSHOT_H
