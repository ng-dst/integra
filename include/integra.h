#ifndef INTEGRA_INTEGRA_H
#define INTEGRA_INTEGRA_H

#include <windows.h>
#include "cjson.h"

// Default: 30 minutes
#ifndef DEFAULT_CHECK_INTERVAL_MS
#define DEFAULT_CHECK_INTERVAL_MS (30 * 60 * 1000)
#endif

void ServiceLoop(HANDLE stopEvent);

void VerifyObject(cJSON* jsonObject);
void VerifyNodeFile(cJSON* jsonNode, HANDLE hBase);
void VerifyNodeReg(cJSON* jsonNode, HKEY hBase);

#endif //INTEGRA_INTEGRA_H
