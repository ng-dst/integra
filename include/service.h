#ifndef INTEGRA_SERVICE_H
#define INTEGRA_SERVICE_H

#include <windows.h>

int SvcInstall();
int SvcUninstall();

DWORD WINAPI SvcCtrlHandlerEx(DWORD, DWORD, LPVOID, LPVOID);
void WINAPI SvcMain(DWORD, LPTSTR*);

void ReportSvcStatus(DWORD, DWORD, DWORD);
void SvcInit(DWORD, LPTSTR*);

#endif //INTEGRA_SERVICE_H
