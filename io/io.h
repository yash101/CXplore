#ifndef _IO_H
#define _IO_H __FILE__
#include "TcpServer.h"
bool isSocketEngineInitialized();
void initializeSocketEngine();

namespace io
{
#ifdef _WIN32
    typedef unsigned int fd_t;
#else
    typedef int fd_t;
#endif
}

#endif