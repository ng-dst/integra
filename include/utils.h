#ifndef INTEGRA_UTILS_H
#define INTEGRA_UTILS_H

#include <windows.h>
#include "cjson.h"

#define OBJECT_FILE 0
#define OBJECT_REGISTRY 1

#define INTEGRA_CHECK_ONCE INVALID_HANDLE_VALUE

cJSON* ReadJSON(LPCTSTR path);
HKEY ParseRootHKEY(LPCTSTR szPath);

int AddObjectToOL(LPCTSTR szName, DWORD dwType, LPCTSTR szPath);
int RemoveObjectFromOL(LPCTSTR szName);
int UpdateObjectInOL(LPCTSTR szName);
int PrintObjectsInOL();
int FindIndexByNameInOL(cJSON* jsonObjectList, LPCTSTR szName);

#endif //INTEGRA_UTILS_H
