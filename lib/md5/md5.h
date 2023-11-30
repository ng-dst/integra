#ifndef INTEGRA_MD5_H
#define INTEGRA_MD5_H

#include <windows.h>

#define MD5LEN  16

DWORD MD5_FileHashDigest(HANDLE hFile, LPTSTR szDigestBuf);
DWORD MD5_MemHashDigest(LPBYTE pbBuf, DWORD dwLen, LPTSTR szDigestBuf);

#endif //INTEGRA_MD5_H
