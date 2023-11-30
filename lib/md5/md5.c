#include <stdio.h>
#include "md5.h"

#define BUFSIZE 1024


DWORD MD5_FileHashDigest(HANDLE hFile, LPTSTR szDigestBuf) {
    /**
     * @brief Compute MD5 from file contents by handle
     *
     * @details https://learn.microsoft.com/en-us/windows/win32/seccrypto/example-c-program--creating-an-md-5-hash-from-file-content
     */
    DWORD dwStatus = 0;
    BOOL bResult = FALSE;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE rgbFile[BUFSIZE];
    DWORD cbRead = 0;
    BYTE rgbHash[MD5LEN];
    DWORD cbHash = 0;
    CHAR rgbDigits[] = "0123456789abcdef";

    // Get handle to the crypto provider
    if (!CryptAcquireContext(&hProv,
                             NULL,
                             NULL,
                             PROV_RSA_FULL,
                             CRYPT_VERIFYCONTEXT))
    {
        dwStatus = GetLastError();
        return dwStatus;
    }

    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        dwStatus = GetLastError();
        CryptReleaseContext(hProv, 0);
        return dwStatus;
    }

    while ((bResult = ReadFile(hFile, rgbFile, BUFSIZE,&cbRead, NULL))) {
        if (!cbRead) break;

        if (!CryptHashData(hHash, rgbFile, cbRead, 0)) {
            dwStatus = GetLastError();
            CryptReleaseContext(hProv, 0);
            CryptDestroyHash(hHash);
            return dwStatus;
        }
    }

    if (!bResult) {
        dwStatus = GetLastError();
        CryptReleaseContext(hProv, 0);
        CryptDestroyHash(hHash);
        return dwStatus;
    }

    cbHash = MD5LEN;
    if (CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0)) {
        for (DWORD i = 0; i < cbHash; i++)
            sprintf(szDigestBuf + 2*i, "%c%c", rgbDigits[rgbHash[i] >> 4], rgbDigits[rgbHash[i] & 0xf]);
    }
    else dwStatus = GetLastError();

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    return dwStatus;
}


DWORD MD5_MemHashDigest(LPBYTE pbBuf, DWORD dwLen, LPTSTR szDigestBuf) {
    /**
     * @brief Compute MD5 from memory buffer
     */
    DWORD dwStatus = 0;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE rgbHash[MD5LEN];
    DWORD cbHash = 0;
    CHAR rgbDigits[] = "0123456789abcdef";

    // Get handle to the crypto provider
    if (!CryptAcquireContext(&hProv,
                             NULL,
                             NULL,
                             PROV_RSA_FULL,
                             CRYPT_VERIFYCONTEXT)) {
        dwStatus = GetLastError();
        return dwStatus;
    }

    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        dwStatus = GetLastError();
        CryptReleaseContext(hProv, 0);
        return dwStatus;
    }

    if (!CryptHashData(hHash, pbBuf, dwLen, 0)) {
        dwStatus = GetLastError();
        CryptReleaseContext(hProv, 0);
        CryptDestroyHash(hHash);
        return dwStatus;
    }

    cbHash = MD5LEN;
    if (CryptGetHashParam(hHash, HP_HASHVAL, rgbHash, &cbHash, 0)) {
        for (DWORD i = 0; i < cbHash; i++)
            sprintf(szDigestBuf + 2*i, "%c%c", rgbDigits[rgbHash[i] >> 4], rgbDigits[rgbHash[i] & 0xf]);
    }
    else dwStatus = GetLastError();

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    return dwStatus;
}