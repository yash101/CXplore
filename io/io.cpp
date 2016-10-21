#include "io.h"
#include <stdio.h>

//Windows Initialization
#ifdef _WIN32

#pragma comment(lib, "Ws2_32.lib")
#include <WinSock2.h>
#include <Windows.h>

static WSADATA WsaData;
static WORD WsaVersionRequested = NULL;
static int WsaError = 0;
static bool IsWsaInitialized = false;

void initializeSocketEngine()
{
    if(!IsWsaInitialized)
    {
        WsaVersionRequested = MAKEWORD(2, 2);
        WsaError = (int) WSAStartup(WsaVersionRequested, &WsaData);

        if(WsaError)
        {
            fprintf(stderr, "[ERROR][FATAL][SOCK] Unable to find/load WinSock successfully! Error code: [%d]\n", WsaError);
            exit(EXIT_FAILURE);
        }

        IsWsaInitialized = true;
    }
}

bool isSocketEngineInitialized()
{
    return IsWsaInitialized;
}

//POSIX (linux) initialization
#else

void initializeSocketEngine()
{}

bool isSocketEngineInitialized()
{
    return true;
}
#endif