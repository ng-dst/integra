#ifndef INTEGRA_INTEGRA_H
#define INTEGRA_INTEGRA_H

#include <windows.h>
#include "cjson.h"

void ServiceLoop(HANDLE stopEvent);

void VerifyObject(cJSON* jsonObject);
void VerifyNodeFile(cJSON* jsonNode, HANDLE hBase);
void VerifyNodeReg(cJSON* jsonNode, HKEY hBase);

#endif //INTEGRA_INTEGRA_H
