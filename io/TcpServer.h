#pragma once
#ifndef _TCPSERVER_H
#define _TCPSERVER_H __FILE__
#include <vector>
#include <string>
#include <stddef.h>
#include "io.h"

namespace io
{
    struct _list_inf;
    class TcpServer;
    class TcpServerConnection;

    struct _list_inf
    {
    public:
        void* _address;
        fd_t _fd;
        int _listPort;
        bool _cre_success;
    };

    struct _portinf
    {
    public:
        int port;
        std::string remote_address;
        std::string local_address;
    };

    class TcpServer
    {
        friend class io::TcpServerConnection;
    private:
        //Tracks all the pointers we use so we can free them
        //safely and easily
        std::vector<void*> _track_alloc;

        //Stores data for each listener
        std::vector<_list_inf> _listeners;

        //Internal structures and data
        bool _isRunning;
        void* _timeout;
        int _tcpConnQueue;
        size_t _nConnCli;
        size_t _nMaxConnCli;
        void* _fdSet;
        int _nfds;
        
        //Number of sockets that were properly initialized
        int _socketsInitializedSuccessfully;

        //Mutexes to protect against multiple concurrent writes
        void* _nConnCliMut;
        void* _nMaxConnCliMut;
        void* _timeoutMut;

        //GC
        void cleanup_malloc();

    protected:
        void* malloc_int(size_t nBytes);

    public:
        TcpServer();
        virtual ~TcpServer();

        bool addListeningPort(int port);
        long getTimeoutSeconds();
        long getTimeoutMicroseconds();
        void setTimeout(long secs, long micros);
        size_t getNumberConnectedClients();
        size_t getMaxNumberConnectedClients();
        void setMaxNumberConnectedClients(size_t n);
        int getTcpConnectionQueueSize();
        bool setTcpConnectionQueueSize(int n);
        bool isServerRunning();
        int startServer();
        std::vector<_list_inf> getInitializedSockets();

        TcpServerConnection* accept();
    };

    class TcpServerConnection
    {
        friend class io::TcpServer;
    private:
        struct _portinf _portinfo;
        fd_t _fd;
        void* _address;

    public:
        TcpServerConnection();
        ~TcpServerConnection();
        inline struct _portinf getPortinfo()
        {
            return _portinfo;
        }
    };
}

#endif