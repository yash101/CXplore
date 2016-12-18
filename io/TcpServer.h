#pragma once
#ifndef _TCPSERVER_H
#define _TCPSERVER_H __FILE__
#include <vector>
#include <string>
#include <stddef.h>
#include "io.h"

namespace io
{
#ifdef _WIN32
    typedef int _sa_ret;
#else
    typedef ssize_t _sa_ret;
#endif

    struct _list_inf;
    class TcpServer;
    class TcpServerConnection;

#ifdef _WIN32
    typedef unsigned int fd_t;
#else
    typedef int fd_t;
#endif

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

        //Flag that is set if we are shutting down the server
        bool _shutServer;

        //GC
        void cleanup_malloc();

    protected:
        void* malloc_int(size_t nBytes);
        _list_inf getListenerInfo(int fd);

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
        struct _list_inf _listinfo;

        TcpServer* _parentServer;
        bool _reduceCliCt;

        fd_t _fd;
        void* _address;

    protected:
        _sa_ret recv_int(void* buf, size_t len, int flags);
        _sa_ret send_int(void* buf, size_t len, int flags);

    public:
        static const int FILL_BUFFER = 0;
        TcpServerConnection();
        ~TcpServerConnection();

        _sa_ret readline(std::string& buffer, char end);
        _sa_ret readline(char* buffer, size_t buffer_len, char end);

        _sa_ret read(char& ch);
        _sa_ret read(std::string& str, size_t maxlen);
        _sa_ret read(void* buffer, size_t buffer_len);

        _sa_ret peek(char& ch);
        _sa_ret peek(std::string& str, size_t maxlen);
        _sa_ret peek(void* buffer, size_t buffer_len);

        _sa_ret write(char ch);
        _sa_ret write(std::string& data);
        _sa_ret write(const char* str);
        _sa_ret write(void* data, size_t length);

        inline struct _portinf getPortinfo()
        {
            return _portinfo;
        }

        inline fd_t getSocketDescriptor()
        {
            return _fd;
        }
    };

    bool sockError(_sa_ret fd);
}

#endif