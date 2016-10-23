#include <stdlib.h>
#include <string.h>
#include <mutex>
#include <new>

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
#else
#define SOCK_ERR(x)(x < 0)
#define SOCK_GOOD(x)(x >= 0)
#define BAD_SOCKET (-1)
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
    _nfds(0)
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
    //Clean up all allocations
    if(_nConnCliMut) delete _nConnCliMut;
    if(_nMaxConnCliMut) delete _nMaxConnCliMut;
    if(_timeoutMut) delete _timeoutMut;
    //Anything else
    cleanup_malloc();
}

//Free all the buffers we allocated
void io::TcpServer::cleanup_malloc()
{
    for(std::vector<void*>::const_iterator it = _track_alloc.begin(); it != _track_alloc.end(); ++it)
    {
        free(*it);
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
        _listeners[i]._fd = socket(AF_INET, SOCK_STREAM | O_NONBLOCK, IPPROTO_TCP);
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
        ( ( struct sockaddr_in6* ) _listeners[i]._address )->sin6_family = AF_INET;
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

    while(true)
    {
        //Timeout for select()
        struct timeval tv = select_timeout;
        fd_set dup = *( (fd_set*) _fdSet );

        //Allocate space for a new connection object
        io::TcpServerConnection* connection = new (std::nothrow) io::TcpServerConnection;
        if(!connection)
        {
            if(_reduceclict)
            {
                base::AutoMutex<std::mutex>(( std::mutex* ) _nConnCliMut, base::LOCKED);
                _nConnCli--;
            }
            return NULL;
        }

        //Protects the allocation until unnecessary
        _autodest<io::TcpServerConnection> terminator(connection);

        //We incremented our client connections
        connection->_reduceCliCt = _reduceclict;

        //Perform socket selection
        int ret = select(_nfds, (fd_set*) &dup, NULL, NULL, &tv);
        if(SOCK_ERR(ret))
        {
        }

        //The socket which is ready
        fd_t gfd = BAD_SOCKET;
        //Find the ready socket
        for(auto it = _listeners.begin(); it != _listeners.end(); ++it)
        {
            if(FD_ISSET(it->_fd, _nfds))
            {
                gfd = it->_fd;
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
        const static sso_tp sso = ( sso_tp ) sizeof(struct sockaddr_in6);

        //Accept the new connection
        connection->_fd = ::accept(gfd,
                                  (struct sockaddr*) connection->_address,
                                  (sso_tp*) &sso
        );
        //If there was an error in accepting the connection...
        if(SOCK_ERR(connection->_fd))
        {
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
           {
               continue;
           }
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
           fcntl(connection->_fd, F_SETFL, flags);
#endif
           {
                continue;
           }
        }

        //Information about this server to be saved in the connection class/struct
        connection->_parentServer = this;
        connection->_listinfo = getListenerInfo(gfd);
        connection->_portinfo.port = connection->_listinfo._listPort;

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