#pragma once

#include "vchannel.hpp"

INT _cdecl wmain(INT argc, __in_ecount(argc) WCHAR** argv)
{
    DWORD rc;
    HANDLE hFile;

    rc = OpenDynamicChannel("DVC_Sample", &hFile);
    if (ERROR_SUCCESS != rc)
    {
        return 0;
    }

    DWORD dwThreadId;
    HANDLE hReadThread = CreateThread(
        NULL,
        0,
        ReadThread,
        hFile,
        0,
        &dwThreadId);

    HANDLE hWriteThread = CreateThread(
        NULL,
        0,
        WriteThread,
        hFile,
        0,
        &dwThreadId);

    HANDLE ah[] = { hReadThread, hWriteThread };
    WaitForMultipleObjects(2, ah, TRUE, INFINITE);

    CloseHandle(hReadThread);
    CloseHandle(hWriteThread);
    CloseHandle(hFile);

    return 1;
}

/*
*  Open a dynamic channel with the name given in szChannelName.
*  The output file handle can be used in ReadFile/WriteFile calls.
*/
DWORD OpenDynamicChannel(
    LPCSTR szChannelName,
    HANDLE* phFile)
{
    HANDLE hWTSHandle = NULL;
    HANDLE hWTSFileHandle;
    PVOID vcFileHandlePtr = NULL;
    DWORD len;    

    auto do_free_and_return = [&] (const auto rc)
    {
            if (vcFileHandlePtr)
            {
                WTSFreeMemory(vcFileHandlePtr);
            }
            if (hWTSHandle)
            {
                WTSVirtualChannelClose(hWTSHandle);
            }
            return rc;
    };

    hWTSHandle = WTSVirtualChannelOpenEx(
        WTS_CURRENT_SESSION,
        (LPSTR)szChannelName,
        WTS_CHANNEL_OPTION_DYNAMIC);
    if (NULL == hWTSHandle)    
        return do_free_and_return(GetLastError());

    BOOL fSucc = WTSVirtualChannelQuery(
        hWTSHandle,
        WTSVirtualFileHandle,
        &vcFileHandlePtr,
        &len);
    if (!fSucc)
        return do_free_and_return(GetLastError());
    if (len != sizeof(HANDLE))
        return do_free_and_return(ERROR_INVALID_PARAMETER);

    hWTSFileHandle = *(HANDLE*)vcFileHandlePtr;

    fSucc = DuplicateHandle(
        GetCurrentProcess(),
        hWTSFileHandle,
        GetCurrentProcess(),
        phFile,
        0,
        FALSE,
        DUPLICATE_SAME_ACCESS);
    if (!fSucc)    
        return do_free_and_return(GetLastError());

    return do_free_and_return(ERROR_SUCCESS);
}

/*
*  Write a series of random messages into the dynamic virtual channel.
*/
DWORD WINAPI WriteThread(PVOID param)
{
    HANDLE  hFile;
    BYTE    WriteBuffer[MAX_MSG_SIZE];
    DWORD   dwWritten;
    BOOL    fSucc;
    BYTE    b = 0;
    HANDLE  hEvent;

    hFile = (HANDLE)param;

    hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    for (ULONG msgSize = START_MSG_SIZE;
        msgSize < MAX_MSG_SIZE;
        msgSize += STEP_MSG_SIZE)
    {
        OVERLAPPED  Overlapped = { 0 };
        Overlapped.hEvent = hEvent;

        for (ULONG i = 0; i < msgSize; i++, b++)
        {
            WriteBuffer[i] = b;
        }

        fSucc = WriteFile(
            hFile,
            WriteBuffer,
            msgSize,
            &dwWritten,
            &Overlapped);
        if (!fSucc)
        {
            if (GetLastError() == ERROR_IO_PENDING)
            {
                DWORD dw = WaitForSingleObject(Overlapped.hEvent, _MAX_WAIT);
                _ASSERT(WAIT_OBJECT_0 == dw);
                fSucc = GetOverlappedResult(
                    hFile,
                    &Overlapped,
                    &dwWritten,
                    FALSE);
            }
        }

        if (!fSucc)
        {
            DWORD error = GetLastError();
            return error;
        }

        _ASSERT(dwWritten == msgSize);
    }

    return 0;
}

/*
*  Read the data from the dynamic virtual channel reconstruct the original
*  message and verify its content.
*/
DWORD WINAPI ReadThread(PVOID param)
{
    HANDLE hFile;
    BYTE ReadBuffer[CHANNEL_PDU_LENGTH];
    DWORD dwRead;
    BYTE b = 0;
    CHANNEL_PDU_HEADER* pHdr;
    BOOL fSucc;
    HANDLE hEvent;

    hFile = (HANDLE)param;
    pHdr = (CHANNEL_PDU_HEADER*)ReadBuffer;

    hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

    for (ULONG msgSize = START_MSG_SIZE;
        msgSize < MAX_MSG_SIZE;
        msgSize += STEP_MSG_SIZE)
    {
        OVERLAPPED  Overlapped = { 0 };
        DWORD TotalRead = 0;
        do {
            Overlapped.hEvent = hEvent;

            // Read the entire message.
            fSucc = ReadFile(
                hFile,
                ReadBuffer,
                sizeof(ReadBuffer),
                &dwRead,
                &Overlapped);
            if (!fSucc)
            {
                if (GetLastError() == ERROR_IO_PENDING)
                {
                    DWORD dw = WaitForSingleObject(Overlapped.hEvent, INFINITE);
                    _ASSERT(WAIT_OBJECT_0 == dw);
                    fSucc = GetOverlappedResult(
                        hFile,
                        &Overlapped,
                        &dwRead,
                        FALSE);
                }
            }

            if (!fSucc)
            {
                DWORD error = GetLastError();
                return error;
            }

            ULONG packetSize = dwRead - sizeof(*pHdr);
            TotalRead += packetSize;
            PBYTE pData = (PBYTE)(pHdr + 1);
            for (ULONG i = 0; i < packetSize; pData++, i++, b++)
            {
                _ASSERT(*pData == b);
            }

            _ASSERT(msgSize == pHdr->length);

        } while (0 == (pHdr->flags & CHANNEL_FLAG_LAST));

        _ASSERT(TotalRead == msgSize);
    }

    return 0;
}
