#ifndef INTEGRA_CFG_H
#define INTEGRA_CFG_H

#include <windows.h>

WINBOOL InitRegPaths();

LPTSTR GetOLFilePath();
WINBOOL SetOLFilePath(LPCTSTR path);

DWORD GetCheckInterval();
WINBOOL SetCheckInterval(DWORD dwValueMs);

#endif //INTEGRA_CFG_H
