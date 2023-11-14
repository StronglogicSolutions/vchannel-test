#pragma once

#include <windows.h>
#include <wtsapi32.h>
#include <pchannel.h>
#include <crtdbg.h>

#pragma comment(lib, "wtsapi32.lib")

#define _MAX_WAIT       60000
#define MAX_MSG_SIZE    0x20000
#define START_MSG_SIZE  4
#define STEP_MSG_SIZE   113

DWORD OpenDynamicChannel(LPCSTR szChannelName, HANDLE* phFile);
DWORD WINAPI WriteThread(PVOID param);
DWORD WINAPI ReadThread(PVOID param);

INT _cdecl wmain(INT argc, __in_ecount(argc) WCHAR** argv);