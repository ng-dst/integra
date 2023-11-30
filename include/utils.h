#ifndef INTEGRA_UTILS_H
#define INTEGRA_UTILS_H

#include <windows.h>
#include "cjson.h"

cJSON* ReadJSON(LPCTSTR path);

DWORD GetRegPathFromHKEY(HKEY hBase, LPTSTR szPath, DWORD cbSize);
HKEY ParseRootHKEY(LPCTSTR szPath);

int AddObjectToOL(LPCTSTR szName, DWORD dwType, LPCTSTR szPath);
int RemoveObjectFromOL(LPCTSTR szName);
int UpdateObjectInOL(LPCTSTR szName);
int PrintObjectsInOL();
int FindIndexByNameInOL(cJSON* jsonObjectList, LPCTSTR szName);

#endif //INTEGRA_UTILS_H
