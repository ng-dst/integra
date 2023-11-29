#ifndef INTEGRA_INTEGRA_H
#define INTEGRA_INTEGRA_H

#include <windows.h>
#include "cjson.h"

cJSON* ReadJSON(LPCTSTR path);

void ServiceLoop(HANDLE stopEvent);
WINBOOL VerifyObject(cJSON* jsonObject);
WINBOOL VerifyNode(cJSON* jsonNode, DWORD dwType, LPCTSTR szPath);

#endif //INTEGRA_INTEGRA_H
