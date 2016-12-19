#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <new>

#include <stdio.h>

#include "TcpServer.h"
#include "../base/automtx.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#else
#include <sys/socket.h>
#endif

#ifdef _WIN32
#define SOCK_ERR(x)(x == SOCKET_ERROR)
#define SOCK_GOOD(x)(x != SOCKET_ERROR)
#define BAD_SOCKET INVALID_SOCKET

#define MSG_NOSIGNAL 0
#else
#define SOCK_ERR(x)(x < 0)
#define SOCK_GOOD(x)(x >= 0)
#define BAD_SOCKET (-1)
#define SOCKET_ERROR -1
#endif

#ifndef TCPSERVER_SELECTSEC
#define TCPSERVER_SELECTSEC 10
#endif
#ifndef TCPSERVER_SELECTUSEC
#define TCPSERVER_SELECTUSEC 0
#endif

#ifdef _WIN32
#define SHUT_RDRW 2
#endif

bool io::sockError(io::_sa_ret x)
{
    return SOCK_ERR(x);
}

static struct timeval select_timeout;

//Initializes the server object
io::TcpServer::TcpServer() :
    _isRunning(false),
    _timeout(NULL),
    _tcpConnQueue(3),
    _nConnCli(0),
    _nMaxConnCli(0),
    _nConnCliMut(NULL),
    _nMaxConnCliMut(NULL),
    _timeoutMut(NULL),
    _socketsInitializedSuccessfully(0),
    _fdSet(NULL),
    _nfds(0),
    _shutServer(false)
{
    //Initialize socket engine
    initializeSocketEngine();

    select_timeout.tv_sec = TCPSERVER_SELECTSEC;
    select_timeout.tv_usec = TCPSERVER_SELECTUSEC;

    //Allocate timeout
    if(!( _timeout = malloc_int(sizeof(struct timeval)) )) return;
    memset((void*) _timeout, 0, sizeof(struct timeval));
    if(!( _fdSet = malloc_int(sizeof(fd_set)) )) return;
    memset((void*) _fdSet, 0, sizeof(fd_set));
    //Allocate mutexes
    if(!( _nConnCliMut = new ( std::nothrow ) std::mutex )) return;
    if(!( _nMaxConnCliMut = new ( std::nothrow ) std::mutex )) return;
    if(!( _timeoutMut = new ( std::nothrow ) std::mutex )) return;
}

//Destroys the server object
io::TcpServer::~TcpServer()
{
    _shutServer = true;
    for(size_t i = 0; i < _listeners.size(); i++)
    {
        delete _listeners[i]._address;
#ifdef _WIN32
        closesocket(_listeners[i]._fd);
#else
        close(_listeners[i]._fd);
#endif
    }

    //Clean up all allocations
    if(_nConnCliMut) delete _nConnCliMut;
    _nConnCliMut = NULL;
    if(_nMaxConnCliMut) delete _nMaxConnCliMut;
    _nMaxConnCliMut = NULL;
    if(_timeoutMut) delete _timeoutMut;
    _timeoutMut = NULL;
    //Anything else
    cleanup_malloc();
}

//Free all the buffers we allocated
void io::TcpServer::cleanup_malloc()
{
    for(size_t i = 0; i < _track_alloc.size(); i++)
    {
        free(_track_alloc[i]);
        _track_alloc[i] = NULL;
    }
}

//Allocates memory and remembers that we did so, to help with memory cleanup
void* io::TcpServer::malloc_int(size_t nBytes)
{
    void* ret = malloc(nBytes);
    if(!ret) return NULL;
    _track_alloc.push_back(ret);
    return ret;
}

//Adds a listening port to the server
bool io::TcpServer::addListeningPort(int port)
{
    if(_isRunning) return false;

    struct _list_inf inf;
    if(!( inf._address = malloc_int(sizeof(struct sockaddr_in)) )) return false;
    memset(inf._address, 0, sizeof(struct sockaddr_in));
    inf._fd = BAD_SOCKET;
    inf._listPort = port;
    _listeners.push_back(inf);

    printf("Adding: %d\n", inf._listPort);

    return true;
}

long io::TcpServer::getTimeoutSeconds()
{
    base::AutoMutex<std::mutex>(( std::mutex* ) _timeoutMut);
    return ( (struct timeval*) _timeout )->tv_sec;
}

long io::TcpServer::getTimeoutMicroseconds()
{
    base::AutoMutex<std::mutex>(( std::mutex* ) _timeoutMut);
    return ( (struct timeval*) _timeout )->tv_usec;
}

void io::TcpServer::setTimeout(long secs, long micros)
{
    base::AutoMutex<std::mutex>(( std::mutex* ) _timeoutMut);
    ( (struct timeval*) _timeout )->tv_usec = micros;
    ( (struct timeval*) _timeout )->tv_sec = secs;
}

size_t io::TcpServer::getNumberConnectedClients()
{
    base::AutoMutex<std::mutex>(( std::mutex* ) _nConnCliMut);
    return _nConnCli;
}

size_t io::TcpServer::getMaxNumberConnectedClients()
{
    base::AutoMutex<std::mutex>(( std::mutex* ) _nMaxConnCliMut);
    return _nMaxConnCli;
}

void io::TcpServer::setMaxNumberConnectedClients(size_t n)
{
    base::AutoMutex<std::mutex>(( std::mutex* ) _nMaxConnCliMut);
    _nMaxConnCli = n;
}

int io::TcpServer::getTcpConnectionQueueSize()
{
    return _tcpConnQueue;
}

bool io::TcpServer::setTcpConnectionQueueSize(int n)
{
    if(_isRunning) return false;
    _tcpConnQueue = n;
    return true;
}

bool io::TcpServer::isServerRunning()
{
    return _isRunning;
}

int io::TcpServer::startServer()
{
    //If server is running, WOOPS!
    if(_isRunning) return false;

    //Zero out our fd_set for SELECT()
    FD_ZERO(_fdSet);

    //For each listener that we have
    for(size_t i = 0; i < _listeners.size(); i++)
    {
        //Create the socket
#ifdef _WIN32
#define O_NONBLOCK 0
#endif
        _listeners[i]._fd = socket(AF_INET6, SOCK_STREAM | O_NONBLOCK, IPPROTO_TCP);
        int err = errno;
        if(SOCK_ERR(_listeners[i]._fd))
        {
            //If the socket engine has not been initialized
#ifdef _WIN32
            if(err == WSANOTINITIALISED)
            {
                ::initializeSocketEngine();
            }
#endif

            //Mark as unsuccessful
            _listeners[i]._cre_success = false;
            continue;
        }
        //Mark as successful
        _listeners[i]._cre_success = true;

        //Set the socket to reuse the address (safety issue, but isn't that big of a deal in most cases)
#ifdef _WIN32
        typedef char sso_tp;
#else
        typedef const void sso_tp;
#endif
        const static int reuseaddr_1 = 1;
        if(setsockopt(_listeners[i]._fd, SOL_SOCKET, SO_REUSEADDR, (sso_tp*) &reuseaddr_1, sizeof(sso_tp)))
        {
            err = errno;
#ifdef _WIN32
            closesocket(_listeners[i]._fd);
#else
            close(_listeners[i]._fd);
#endif
            _listeners[i]._cre_success = false;
            continue;
        }

        //Set the sockets as nonblocking sockets
#ifdef _WIN32
        //Enable FIONFIO
        static const unsigned long ioctlmode = 1;
        //Set the value
        if(ioctlsocket(_listeners[i]._fd, FIONBIO, (unsigned long*) &ioctlmode))
//#else             //Commented out because O_NONBLOCK was placed as a flag to socket()
//        //Retrieve flags
//        int flags = fcntl(_listeners[i]._fd, F_GETFL, 0);
//        //Check for error(s)
//        if(flags < 0)
//        {
//            close(_listeners[i]._fd);
//            _listeners[i]._cre_success = false;
//            continue;
//        }
//        //Set our flag option
//        flags &= ~O_NONBLOCK;
//        //Update flags in fcntl
//        if(fcntl(_listeners[i]._fd, F_SETFL, flags) < 0)
#endif
        {
            //Close the socket
#ifdef _WIN32
            closesocket(_listeners[i]._fd);
#else
            close(_listeners[i]._fd);
#endif
            //Unsuccessful
            _listeners[i]._cre_success = false;
            continue;
        }

        //Populate address structures; use malloc_int for painless GC (bound to destructor of this object)
        _listeners[i]._address = (void*) malloc_int(sizeof(struct sockaddr_in6));
        //Chk
        if(!_listeners[i]._address)
        {
#ifdef _WIN32
            closesocket(_listeners[i]._fd);
#else
            close(_listeners[i]._fd);
#endif
            _listeners[i]._cre_success = false;
            continue;
        }
        //Clr
        memset(_listeners[i]._address, 0, sizeof(struct sockaddr_in6));
        //Set
        ( ( struct sockaddr_in6* ) _listeners[i]._address )->sin6_family = AF_INET6;
        ( ( struct sockaddr_in6* ) _listeners[i]._address )->sin6_addr = in6addr_any;
        ( ( struct sockaddr_in6* ) _listeners[i]._address )->sin6_port = htons(_listeners[i]._listPort);
        ( ( struct sockaddr_in6* ) _listeners[i]._address )->sin6_flowinfo = 0;

        //Bind the socket to the address
        if(SOCK_ERR(bind(_listeners[i]._fd, ( struct sockaddr* ) _listeners[i]._address, sizeof(struct sockaddr_in6))))
        {
#ifdef _WIN32
            closesocket(_listeners[i]._fd);
#else
            close(_listeners[i]._fd);
#endif
            _listeners[i]._cre_success = false;
            continue;
        }

        if(SOCK_ERR(listen(_listeners[i]._fd, _tcpConnQueue)))
        {
#ifdef _WIN32
            closesocket(_listeners[i]._fd);
#else
            close(_listeners[i]._fd);
#endif
            _listeners[i]._cre_success = false;
            continue;
        }

        FD_SET(_listeners[i]._fd, _fdSet);
        if(_listeners[i]._fd + 1 > (io::fd_t) _nfds)
            _nfds = _listeners[i]._fd + 1;

        //We successfully initialized n sockets
        _socketsInitializedSuccessfully++;
    }

    _isRunning = true;

    return _socketsInitializedSuccessfully;
}

std::vector<io::_list_inf> io::TcpServer::getInitializedSockets()
{
    return _listeners;
}

template<class T>
class _autodest
{
private:
    T* mem;
public:
    _autodest(T* block) : mem(block)
    {}

    void cancelDestruction()
    {
        mem = NULL;
    }

    void* continueDestruction()
    {
        delete mem;
        return mem = NULL;
    }

    ~_autodest()
    {
        if(mem) delete (T*) mem;
        mem = NULL;
    }
};

io::TcpServerConnection* io::TcpServer::accept()
{
    bool _reduceclict = false;
    //Check to see if the server is configured to handle any more clients
    {
        //These help us safeguard and play with the mutexes
        base::AutoMutex<std::mutex> mtxCur(( std::mutex* ) _nConnCliMut, base::UNLOCKED);
        base::AutoMutex<std::mutex> mtxMax(( std::mutex* ) _nMaxConnCliMut, base::UNLOCKED);
        //Try infinitely until a connected client gets released; break if we are allowed unlimited
        //For unlimited connections: this->setMaxNumberConnectedClients(0)
        do
        {
            //Lock mutexes
            mtxCur.lock();
            mtxMax.lock();
            //Check conditions
            if(_nMaxConnCli == 0 || _nConnCli <= _nMaxConnCli)
            {
                break;
            }
            else
            {
                //Unlock mutexes
                mtxCur.unlock();
                mtxMax.unlock();
                //Pause to prevent using up all CPU cycles waiting
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            //Unlock mutexes (if not prev. unlocked)
            mtxCur.unlock();
            mtxMax.unlock();
        } while(true);
        //Can be a bit safe. The AutoMutex guards against double unlocking
        mtxMax.lock();
        //Set that we are incrementing the counter
        _reduceclict = ( _nMaxConnCli != 0 );
        mtxMax.unlock();
        if(_reduceclict)
        {
            mtxCur.lock();
            //Increment client counter
            _nConnCli++;
        }
        mtxCur.unlock();
    }

    //Try until we are successful; these calls may fail because of benign things
    while(true)
    {
        //Timeout for select(); duplicate because select() often modifies the value(s)
        struct timeval tv = select_timeout;
        fd_set dup = *( (fd_set*) _fdSet );

        //Allocate space for a new connection object
        io::TcpServerConnection* connection = new (std::nothrow) io::TcpServerConnection;
        if(!connection)
        {
            return NULL;
        }

        //Protects the allocation until unnecessary
        _autodest<io::TcpServerConnection> terminator(connection);

        //Perform socket selection
        int ret = select(_nfds, (fd_set*) &dup, NULL, NULL, &tv);
        if(SOCK_ERR(ret))
        {
            int err = errno;
            if(errno == EBADF
               || errno == EINVAL
            ) return NULL;
            continue;
        }
        if(ret == 0)
        {
            continue;
        }

        //The socket which is ready
        fd_t gfd = BAD_SOCKET;
        //Find the ready socket
        for(size_t i = 0; i < _listeners.size(); i++)
        {
            if(FD_ISSET(_listeners[i]._fd, &dup))
            {
                gfd = _listeners[i]._fd;
                break;
            }
        }
        //Check if something's wrong with select();
        if(SOCK_ERR(gfd))
            continue;

#ifdef _WIN32
        typedef int sso_tp;
#else
        typedef socklen_t sso_tp;
#endif

        //Used as an argument for accept
        sso_tp sso = ( sso_tp ) sizeof(struct sockaddr_in6);

        memset(connection->_address, 0, sizeof(struct sockaddr_in6));

        //Accept the new connection
        connection->_fd = ::accept(gfd,
                                  (struct sockaddr*) connection->_address,
                                  (sso_tp*) &sso
        );
        //If there was an error in accepting the connection...
        if(SOCK_ERR(connection->_fd))
        {
            int err = errno;
            //Check for non-recoverable faults
            if(err == EFAULT
               || err == EINVAL
               || err == ENOTSOCK
               || err == EOPNOTSUPP
            ) return NULL;
            continue;
        }

        //Make the socket blocking
        //Windows code
        {
#ifdef _WIN32
           //Mode selection
           unsigned long ioctlmode = 0;
           //Push change
           if(ioctlsocket(connection->_fd, FIONBIO, &ioctlmode))
#else
           //Retrieve old flags
           int flags = fcntl(connection->_fd, F_GETFL, 0);
           //Check if bad
           if(flags < 0)
           {
               continue;
           }
           //Set the O_NONBLOCK bit
           flags |= O_NONBLOCK;
           //Set the flags
           if(fcntl(connection->_fd, F_SETFL, flags) == -1)
#endif
           {
                continue;
           }
        }

        //Server information
        connection->_listinfo = getListenerInfo(gfd);
        connection->_portinfo.port = connection->_listinfo._listPort;
        //We incremented our client connections; we don't want to decrement them
        connection->_reduceCliCt = _reduceclict;
        connection->_parentServer = this;

        //Everything is successful! Our connection object looks beautiful!
        terminator.cancelDestruction();
        return connection;
     }
}

io::_list_inf io::TcpServer::getListenerInfo(int fd)
{
    for(std::vector<io::_list_inf>::const_iterator it = _listeners.begin(); it != _listeners.end(); ++it)
    {
        if(it->_fd == fd)
            return *it;
    }
    return _list_inf();
}

io::TcpServerConnection::TcpServerConnection() :
    _address(NULL),
    _fd(BAD_SOCKET),
    _parentServer(NULL),
    _reduceCliCt(false)
{
    _portinfo.local_address = "";
    _portinfo.remote_address = "";
    _portinfo.port = 0;
    _address = new struct sockaddr_in6;
}

io::TcpServerConnection::~TcpServerConnection()
{
    //Delete the address pointer
    if(_address) delete _address;
    _address = NULL;

    //Close the socket (if good)
    if(SOCK_GOOD(_fd))
    {
        //Gracefully shut down a socket before closing it completely
        shutdown(_fd, SHUT_RDRW);
        //Close the socket
#ifdef _WIN32
        closesocket(_fd);
#else
        close(_fd);
#endif
    }

    //Decrement client counter
    if(_reduceCliCt && _parentServer)
    {
        base::AutoMutex<std::mutex> mtx((std::mutex*) _parentServer->_nConnCliMut);
        _parentServer->_nConnCli--;
        mtx.unlock();
    }
}

//Recieve bytes (with a flag)
io::_sa_ret io::TcpServerConnection::recv_int(void* buf, size_t len, int flags)
{
    return ::recv(_fd, (char*) buf, len, flags);
}

//Send bytes (with a flag)
io::_sa_ret io::TcpServerConnection::send_int(void* buf, size_t len, int flags)
{
    io::_sa_ret ret = ::send(_fd, (char*) buf, len, flags);
    return ret;
}

//Reads a line, terminated by [char] end
io::_sa_ret io::TcpServerConnection::readline(std::string& buffer, char end)
{
    //This will never equal end :)
    char ch;
    io::_sa_ret ct = 0;

    while(true)
    {
        io::_sa_ret ret = recv_int(&ch, sizeof(char), MSG_NOSIGNAL);
        if(SOCK_ERR(ret))
        {
            int err = errno;
            if(err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
            {
                continue;
            }
            else
            {
                return ret;
            }
        }
        if(ch == end) return ct;
        buffer += ch;
        ct++;
    }
    return ct;
}

io::_sa_ret io::TcpServerConnection::readline(char* buffer, size_t buffer_len, char end)
{
    char last = ~end;
    size_t pos = 0;
    char* wt = buffer;
    while(pos++ < buffer_len)
    {
        //Read a byte
        io::_sa_ret ret = recv_int(wt, sizeof(char), MSG_NOSIGNAL);
        //Check for any possible errors encountered
        if(SOCK_ERR(ret))
        {
            //Gather error
            int err = errno;
            //Check what it is
            if(err == EAGAIN || err == EWOULDBLOCK || err == EINTR)
            {
                continue;
            }
            else
            {
                *wt = '\0';
                return ret;
            }
        }
        if(*wt == end)
        {
            *++wt = '\0';
            return pos;
        }
        wt++;
    }
    wt[buffer_len - 1] = '\0';
    return buffer_len - 1;
}

io::_sa_ret io::TcpServerConnection::read(char& ch)
{
    //MSG_NOSIGNAL is defined as zero in windows (above within this file)
    return this->recv_int(&ch, sizeof(char), MSG_NOSIGNAL);
}

io::_sa_ret io::TcpServerConnection::read(std::string& str, size_t mxlen)
{
    void* data = ( void* ) new char[mxlen];
    //Destroys the buffer once we've used it
    _autodest<char> dest((char*) data);
    //MSG_NOSIGNAL is defined as zero in windows (above within this file)
    io::_sa_ret ret = this->recv_int(data, mxlen, MSG_NOSIGNAL);
    if(SOCK_ERR(ret))
    {
        return ret;
    }
    str = (const char*) data;
    return ret;
}

io::_sa_ret io::TcpServerConnection::read(void* buffer, size_t buffer_len)
{
    while(true)
    {
        io::_sa_ret ret = recv_int(buffer, buffer_len - 1, MSG_NOSIGNAL);
        if(SOCK_ERR(ret))
        {
            int err = errno;
            if(err == EINTR)
                continue;
        }
        ( (char*) buffer )[ret] = '\0';
        return ret;
    }
}

io::_sa_ret io::TcpServerConnection::peek(char& ch)
{
    while(true)
    {
        io::_sa_ret ret = recv_int(&ch, sizeof(char), MSG_NOSIGNAL);
        if(SOCK_ERR(ret))
        {
            int err = errno;
            if(err == EINTR)
                continue;
        }
        return ret;
    }
}

io::_sa_ret io::TcpServerConnection::peek(std::string& str, size_t mxlen)
{
    char* ch = new char[mxlen];
    _autodest<char> dest(ch);

    while(true)
    {
        io::_sa_ret ret = recv_int(ch, mxlen, MSG_NOSIGNAL | MSG_PEEK);
        if(SOCK_ERR(ret))
        {
            int err = errno;
            if(err == EINTR)
                continue;
        }
        if(ret < (io::_sa_ret) mxlen - 1)
            ch[ret] = '\0';

        str = ret;
        return ret;
    }
}

io::_sa_ret io::TcpServerConnection::peek(void* buffer, size_t buffer_len)
{
    while(true)
    {
        io::_sa_ret ret = recv_int(buffer, buffer_len - 1, MSG_NOSIGNAL | MSG_PEEK);
        if(SOCK_ERR(ret))
        {
            int err = errno;
            if(err == EINTR)
                continue;
        }
        ( (char*) buffer )[ret] = '\0';
        return ret;
    }
}

io::_sa_ret io::TcpServerConnection::write(char ch)
{
    io::_sa_ret ret;
    do
    {
        ret = send_int(&ch, sizeof(char), MSG_NOSIGNAL);
        if(SOCK_ERR(ret))
        {
            int err = errno;
            if(err == EINTR || err == ENOBUFS || err == ENOMEM)
            {
                continue;
            }
        }
        return ret;
    } while(!SOCK_ERR(ret));
    return ret;
}

io::_sa_ret io::TcpServerConnection::write(std::string& data)
{
    while(true)
    {
        io::_sa_ret ret = send_int((void*) data.c_str(), data.size(), MSG_NOSIGNAL);
        if(SOCK_ERR(ret))
        {
            int err = errno;
            if(err == EINTR || err == ENOBUFS || err == ENOMEM)
                continue;
            return ret;
        }
        return ret;
    }
}

io::_sa_ret io::TcpServerConnection::write(const char* str)
{
    while(true)
    {
        io::_sa_ret ret = send_int((void*) str, strlen(str), MSG_NOSIGNAL);
        if(SOCK_ERR(ret))
        {
            int err = errno;
            if(err == EINTR || err == ENOBUFS || err == ENOMEM)
                continue;
            return ret;
        }
        return ret;
    }
}

io::_sa_ret io::TcpServerConnection::write(void* data, size_t len)
{
    while(true)
    {
        io::_sa_ret ret = send_int(data, len, MSG_NOSIGNAL);
        if(SOCK_ERR(ret))
        {
            int err = errno;
            if(err == EINTR || err == ENOBUFS || err == ENOMEM)
                continue;
            return ret;
        }
        return ret;
    }
}

io::_sa_ret io::TcpServerConnection::sendfile(FILE* fd, size_t write_len)
{
#define DISABLE_SENDFILE_LINUX

#if defined(__linux__) && !defined(DISABLE_SENDFILE_LINUX)
#error "SENDFILE LOGIC NEEDS TO BE WRITTEN!!!"
#else

#ifndef SENDFILE_CHUNKING_SIZE
#define SENDFILE_CHUNKING_SIZE 4096
#endif

    if(!fd) return SOCKET_ERROR;

    char* chunk = new char[4096];
    _autodest<char> dest(chunk);

    size_t npartchunks = write_len / SENDFILE_CHUNKING_SIZE;

    for(size_t i = 0; i < npartchunks; i++)
    {
        //Read the data
        size_t ret = fread(chunk, sizeof(char), SENDFILE_CHUNKING_SIZE, fd);
        if(ret < SENDFILE_CHUNKING_SIZE) return i * SENDFILE_CHUNKING_SIZE + SOCKET_ERROR;
        //Send the data
        io::_sa_ret ret2 = write((void*) chunk, sizeof(char) * SENDFILE_CHUNKING_SIZE);
        if(SOCK_ERR(ret2))
            return ret2;
    }
    size_t ret = fread(chunk, sizeof(char), write_len % SENDFILE_CHUNKING_SIZE, fd);
    if(ret < write_len % SENDFILE_CHUNKING_SIZE)
        return npartchunks * SENDFILE_CHUNKING_SIZE + ret;
    io::_sa_ret ret2 = write((void*) chunk, sizeof(char) * ret);
    if(SOCK_ERR(ret2))
        return ret2;

    return ( io::_sa_ret ) write_len;
#endif
}