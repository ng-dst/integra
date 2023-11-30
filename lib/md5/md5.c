#include <stdio.h>
#include <tchar.h>
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
     * @brief Compute MD5 from memory buffer and convert to digest
     */
    DWORD dwStatus;
    BYTE rgbHash[MD5LEN];
    CHAR rgbDigits[] = "0123456789abcdef";

    dwStatus = MD5_MemHashRaw(pbBuf, dwLen, rgbHash);
    if (dwStatus == ERROR_SUCCESS)
        for (DWORD i = 0; i < MD5LEN; i++)
            sprintf(szDigestBuf + 2*i, "%c%c", rgbDigits[rgbHash[i] >> 4], rgbDigits[rgbHash[i] & 0xf]);

    return dwStatus;
}


DWORD MD5_MemHashRaw(LPBYTE pbBuf, DWORD dwLen, LPBYTE pbHashBuf) {
    /**
     * @brief Compute raw MD5 and store in pbHashBuf
     *
     * @details Safe to invoke with pbBuf == pbHashBuf
     */

    DWORD dwStatus = 0;
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    DWORD cbHash = 0;

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
    if (!CryptGetHashParam(hHash, HP_HASHVAL, pbHashBuf, &cbHash, 0))
        dwStatus = GetLastError();

    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    return dwStatus;
}


DWORD MD5_RegKeyHashDigest(HKEY hkBaseKey, LPTSTR szDigestBuf) {
    /**
     * @brief Get MD5 from registry key's contents
     *
     * @details
     *
     *  Hash for key is computed as:
     *      MD5( MD5(valueName1)^...^MD5(valueNameN) ^ MD5[MD5(keyName1)^...^MD5(keyNameM)] )
     *
     *  where  valueName1...valueNameN  -  values in key
     *         keyName1...keyNameM      -  sub-keys contained in key
     *          ^                       -  XOR operation
     */

    DWORD dwIndex, dwSize;
    TCHAR szSlaveName[MAX_PATH];

    BYTE pbHashBuf[MD5LEN];
    BYTE pbXorHash[MD5LEN] = {0};

    // Iterate over sub-keys
    dwIndex = 0;
    while (ERROR_SUCCESS == RegEnumKey(hkBaseKey, dwIndex, szSlaveName, MAX_PATH)) {
        // MD5(keyName1)^...^MD5(keyNameM)
        MD5_MemHashRaw(szSlaveName, _tcslen(szSlaveName), pbHashBuf);
        for (int i = 0; i < MD5LEN; i++)
            pbXorHash[i] ^= pbHashBuf[i];
        dwIndex++;
    }

    // MD5( MD5(keyName1)^...^MD5(keyNameM) )
    MD5_MemHashRaw(pbXorHash, MD5LEN, pbXorHash);

    // Iterate over values
    dwSize = MAX_PATH;
    dwIndex = 0;
    while (ERROR_SUCCESS == RegEnumValue(hkBaseKey, dwIndex, szSlaveName, &dwSize, NULL, NULL, NULL, NULL)) {
        // ... ^ MD5(valueName1)^...^MD5(valueNameN)
        MD5_MemHashRaw(szSlaveName, _tcslen(szSlaveName), pbHashBuf);
        for (int i = 0; i < MD5LEN; i++)
            pbXorHash[i] ^= pbHashBuf[i];
        dwIndex++;
    }

    // MD5( MD5(valueName1)^...^MD5(valueNameN) ^ MD5[MD5(keyName1)^...^MD5(keyNameM)])
    MD5_MemHashDigest(pbXorHash, MD5LEN, szDigestBuf);
}


DWORD MD5_RegValueHashDigest(HKEY hkBaseKey, LPCTSTR szName, LPTSTR szDigestBuf) {
    /**
     * @brief Compute MD5 from registry value's type and actual value
     *
     * @details
     *
     *  Hash for value is computed as:
     *    MD5( dwType | rbValue )
     *
     *  where  dwType   is  4-byte DWORD (usually little-endian),
     *         rbValue  is  byte buffer for value,
     *          |       is  concat operation
     */
    int res;
    DWORD dwSize, dwType;

    res = RegQueryValueEx(hkBaseKey, szName, NULL, &dwType, NULL, &dwSize);
    if (res != ERROR_SUCCESS && res != ERROR_INSUFFICIENT_BUFFER) return res;

    BYTE* pbRegBuf = malloc(dwSize * sizeof(BYTE) + sizeof(DWORD));
    if (!pbRegBuf) return ERROR_NOT_ENOUGH_MEMORY;

    // Write Type (DWORD)
    memcpy(pbRegBuf, &dwType, sizeof(DWORD));

    // Get value from reg
    res = RegQueryValueEx(hkBaseKey, szName, NULL, NULL, pbRegBuf + sizeof(DWORD), &dwSize);
    if (res != ERROR_SUCCESS) { free(pbRegBuf); return res; }

    // MD5( dwType | rbValue )
    MD5_MemHashDigest(pbRegBuf, dwSize + sizeof(DWORD), szDigestBuf);
    free(pbRegBuf);
    return ERROR_SUCCESS;
}
