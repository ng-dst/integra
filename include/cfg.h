#ifndef INTEGRA_CFG_H
#define INTEGRA_CFG_H

#include <windows.h>

WINBOOL InitRegPaths();

LPTSTR GetOLFilePath();
WINBOOL SetOLFilePath(LPCTSTR path);

#endif //INTEGRA_CFG_H
