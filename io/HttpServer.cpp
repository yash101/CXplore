#include "HttpServer.h"
#include "../util/stringproc.h"
#include <stddef.h>
#include <string.h>
#include <map>

static bool operator==(std::string a, std::string b)
{
    return !strcmp(a.c_str(), b.c_str());
}

void io::http::capitalize(char* string)
{
    //Check if string exists and has data
    if(!string) return;
    if(*string == '\0') return;

    //Capitalize first letter
    *string = toupper(*string);

    char* str = string;

    //Loop until the end of the string
    while(*str != '\0')
    {
        //Check if space of hyphen
        if(isspace(*str) || *str == '-')
        {
            //Capitalize next letter if not NULL
            if(*( str + 1 ) != '\0')
            {
                *(++str + 1) = toupper(*(str + 1));
            }
        }

        //Increment pointer to next char
        str++;
    }
}

void io::Cookie::clearFlag(uint32_t flg)
{
    flags &= ~flg;
}

void io::Cookie::setFlag(uint32_t flg)
{
    flags |= flg;
}

bool io::Cookie::getFlag(uint32_t flg)
{
    return (flags & flg) == flg;
}

void io::http::capitalize(std::string& str)
{
    //Iterate through the entire string
    for(size_t i = 0; i < str.size(); i++)
    {
        //Find the empty spaces or hyphens
        if(isspace(str[i]) || str[i] == '-')
        {
            //Capitalize next letter
            if(i + 1 < str.size())
            {
                str[i + 1] = toupper(str[i + 1]);
            }
        }
    }
}

//Allows faster lookup for the method. When more methods are added, consider
//switching to std::unordered_map
//Only 6 elements is a very fast lookup with O(log(n))
static std::map<std::string, uint32_t> _httpMethodResolve;
static bool _httpMethodResolverInitialized = false;
static void _initializeHttpMethodResolver()
{
    if(_httpMethodResolverInitialized) return;
    _httpMethodResolverInitialized = true;
    using namespace io::flag;
    _httpMethodResolve["GET"] = GET;
    _httpMethodResolve["POST"] = POST;
    _httpMethodResolve["PUT"] = PUT;
    _httpMethodResolve["DELETE"] = DELETE;
    _httpMethodResolve["OPTIONS"] = OPTIONS;
    _httpMethodResolve["CONNECT"] = CONNECT;
}

//Helps with memory management temporarily
//Used when there's the possibility that something might go wild and we MUST deallocate
//Allows pointer to be untracked, allowing someone to use different pointer tracking mechanics
template <class T> class TempSP
{
public:
    //The pointer
    T* ptr;

    //Blank constructor
    inline TempSP() : ptr(nullptr)
    {}

    //Constructor with pointer
    inline TempSP(T* ptr) : ptr(ptr)
    {
        track(ptr);
    }

    //Tracks a new pointer. Overrides the old pointer
    inline T* track(T* ptr)
    {
        T* ret = this->ptr;
        this->ptr = ptr;
        if(!this->ptr) this->ptr = nullptr;
        return ret;
    }
    inline T* untrack()
    {
        T* ret = ptr;
        ptr = nullptr;
        return ret;
    }
    inline T* destruct()
    {
        if(ptr) delete ptr;
        return nullptr;
    }
    inline ~TempSP()
    {
        if(ptr) delete ptr;
    }
    inline T* getPtr()
    {
        return ptr;
    }
};

//Contstructs blank HttpServer object
io::HttpServer::HttpServer() :
    _server(nullptr)
{
    initializeVariables();
}

//Constructs HttpServer object with a io::TcpServer pointer
io::HttpServer::HttpServer(io::TcpServer* server) :
    _server(server)
{
    if(server == NULL) _server = nullptr;
    initializeVariables();
}

//Initialize variables without recreating the initializer list for every constructor :/
//Keep these at a minimum to reduce the amount of RAM used by the server
void io::HttpServer::initializeVariables()
{
    //4 KB should be good for most
    _maxGetLineLength = 4096;
    //1 KB should be good for most
    _maxHeaderLength = 1024;
    //512 B should be good for most
    _maxHeaderNameLength = 512;
    //1023B should be good for most
    _maxHeaderFieldLength = 1022;
    //16 MB is a lot of RAM :/
    _maxPostSize = 16 * 1024 * 1024;
}

//Sets a io::TcpServer pointer, returning the old one
//NOTE: No GC is performed. If done with old server, FREE IT!
io::TcpServer* io::HttpServer::setTcpServer(io::TcpServer* newServer)
{
    io::TcpServer* ret = _server;
    _server = newServer;
    return ret;
}

//Retreives the io::TcpServer pointer to allow changing its settings
io::TcpServer* io::HttpServer::getTcpServer()
{
    return _server;
}

//Waits for and returns a handle to a new request
io::HttpSession* io::HttpServer::operator()()
{
    io::HttpSession* session = new io::HttpSession;
    TempSP<io::HttpSession> sp(session);
    session->_connection = _server->accept();
    session->_httpServer = this;
    session->_tcpServer = _server;

    sp.untrack();
    return session;
}

//Constructs the io::HttpSession object
io::HttpSession::HttpSession() :
    _reqct(0),
    _contReq(false),
    _flags(0),
    _httpServer(nullptr),
    _tcpServer(nullptr),
    _connection(nullptr),
    _wssession(nullptr)
{}

//Destroys the object and frees any unfreed resources
io::HttpSession::~HttpSession()
{
    //Deallocate, shutdown and close the connection
    if(_connection) delete _connection;
    if(_wssession) delete _wssession;
}

//Checks if a flag is set
bool io::HttpSession::isFlagSet(uint32_t flag)
{
    return (_flags & flag) == flag;
}

//Gets the next request if available or returns false
//Returns true if the object has a valid HTTP request
bool io::HttpSession::nextRequest()
{
    //Check if we are supposed to have another request
    if(_reqct != 0 && !_contReq)
        return false;

    //Increment number of requests we've handled
    _reqct++;

    //Process the request
    if(!parseGetLine()) return false;
    if(!parseHeaders()) return false;
    if(!validateHeaders()) return false;

    if(!isFlagSet(io::flag::WEBSOCKET))
    {
        if(!parsePost()) return false;
    }
    else
    {
    }

    //Things worked. Great!
    return true;
}

//Parses the first line of the HTTP request
bool io::HttpSession::parseGetLine()
{
    //Retrieve from server object
    size_t maxGL = _httpServer->_maxGetLineLength;

    //By standards, 64 KB should be plentiful
    if(maxGL == 0) maxGL = 65536;

    //Stupid simple smart pointer
    TempSP<char> sp(new char[maxGL]);
    
    //Read the number of bytes
    auto sret = _connection->readline(sp.getPtr(), maxGL, '\n');
    //Check for errors
    if(io::sockError(sret))
    {
        return false;
    }

    //How long is the data?
    sret = (decltype( sret )) strlen(sp.getPtr());
    //Check to see if no NULL's were received

    //We should be getting at least 'GET / HTTP/1.0\r\n'
    if(sret < (io::_sa_ret) strlen("GET / HTTP/1.0\r\n") || sp.getPtr()[sret - 1] != '\n')
    {
        return false;
    }

    //Get rid of the '\r' and the '\n's
    if(sp.getPtr()[sret - 1] == '\r' || sp.getPtr()[sret - 1] == '\n')
        sp.getPtr()[sret - 1] = '\0';
    if(sp.getPtr()[sret - 2] == '\r' || sp.getPtr()[sret - 2] == '\n')
        sp.getPtr()[sret - 2] = '\0';

    //Break into the parts: {METHOD} {PATH} {HTTP_VER}{\EOF}
    char* tptr = sp.getPtr();
    while(*tptr++ != ' ' && *(tptr) != '\0');
    char* pathSt = tptr;
    while(*tptr++ != ' ' && *(tptr) != '\0');
    char* protoSt = tptr;
    //Insert NULL characters
    *( pathSt - 1 ) = '\0';
    *( protoSt - 1 ) = '\0';

    //Resolve the method used
    //Initialize the resolver (if not already)
    //First use is non-thread safe. Thereafter, read only
    _initializeHttpMethodResolver();
    auto retf = _httpMethodResolve.find(sp.getPtr());
    //Check if it doesn't exist
    if(retf == _httpMethodResolve.end())
    {
        //Woops! failed :'(
        return false;
    }
    //Set the flag for the HTTP method
    _flags |= retf->second;

    //Set the path by copying from the GET line
    path = std::string(pathSt, protoSt - pathSt);

    //Process the path for it's GET parts
    if(!processPath(pathSt, protoSt - sizeof(char)))
    {
        return false;
    }

    //Check if there's a proper HTTP header
    if(!strncmp(protoSt, "HTTP/", strlen("HTTP/")))
    {
        //Remove this comment and warning when fixed. Should be added to any bug trackers as an uncertain case
#pragma message ("Please clean up this code. I haven't taken the time to ensure all pointer reads are bounds-checked in every case")
        //We have, say, "1.0" or "1.1"
        //Find the dot
        char* pos = strchr(protoSt, '.');
        if(!pos) return false;
        char* min = pos + 1;
        *pos = '\0';
        //String to number
        _httpMajor = (char) atoi(protoSt + strlen("HTTP/"));
        _httpMinor = (char) atoi(min);
    }
    else return false;

    return true;
}

//Processes the path component of the first line
bool io::HttpSession::processPath(char* start, char* end)
{
    char* beg = start;
    //Sanity checks
    if(!start || !end || start >= end) return false;

    //Find the query mark or stop at a NULL
    while(start++)
    {
        if(*start == '\0' || *start == '?') break;
    }

    //trimmedPath needs to be set from {beg}=>{start}
    trimmedPath = std::string(beg, start - beg);

    //Check if we had an EOF or the question mark is the last character
    if(*start == '\0' || (*start == '?' && (*(start + 1)) == '\0'))
    {
        return true;
    }

    //Increment start because we are currently pointing to '?'
    start++;

    while(start != end && *start != '\0')
    {
        //We increment this to find the beginning of the sentences
        char* iterator_amp = start;
        char* iterator_eq = start;
        //Find the next equal sign
        while(*( iterator_eq++ ) != '\0' && ( *iterator_eq ) != '=');

        //Find the next ampersand
        while(*( iterator_amp++ ) != '\0' && ( *iterator_amp ) != '&');

        //Check if the equal sign was found after the ampersand
        //True: Name only, no value
        //False: Variable might exist
        //Key: {start}->{iterator_amp}
        //Value: '\0'
        if(iterator_eq > iterator_amp)
        {
            get_queries[std::string(start, iterator_amp - start)] = "";
        }
        //Key: {start}->{iterator_eq - 1}
        //Value: {iterator_eq}->{iterator_amp}
        else if(iterator_eq < iterator_amp)
        {
            //Get rid of the equal sign
            if(*( iterator_eq ) == '=') iterator_eq++;

            get_queries[std::string(start, iterator_eq - start - 1)] =
                std::string(iterator_eq, iterator_amp - iterator_eq);
        }

        start = iterator_amp;
    }

    return true;
}

//Downloads and parses all headers
bool io::HttpSession::parseHeaders()
{
    //Keep a track of how many headers we've received
    size_t hct = 0;
    size_t mhct = _httpServer->_maxHeaderCount;
    size_t mhl = _httpServer->_maxHeaderLength;
    
    //64 KB is WAYYYY more than enough!
    if(mhl == 0) mhl = 65536;
    TempSP<char> hdr(new char[mhl]);

    //Download each individual header
    while(mhct == 0 || hct++ < mhct)
    {
        //Download the header line (ending with a '\n')
        auto cret = _connection->readline(hdr.getPtr(), mhl, '\n');
        //Check for transmission errors. If we got one, woops. Exit!
        if(io::sockError(cret))
        {
            return false;
        }

        //Get rid of the '\r' at the end of the request
        //Remember that the line endings are CRLF
        if(hdr.getPtr()[cret - 2] == '\r') hdr.getPtr()[cret - 2] = '\0';

        //Check if the line is empty, meaning the last header
        if(cret == (decltype( cret )) strlen("\r\n"))
            return true;

        //Find the colon
        char* fin = strchr(hdr.getPtr(), ':');
        char* fin2 = fin;
        if(*fin == '\0')
            continue;
        
        //Truncate the colon(s)
        while(*fin++ == ':' && *fin != '\0');
        //Truncate the spaces
        while(isspace(*fin++) && *fin != '\0');
        fin--;

        //Set the header!
        std::string key(hdr.getPtr(), fin2 - hdr.getPtr());
        util::lowercase(key);

        //Check if it is a cookie
        if(key == "cookie")
        {
            //Process the cookie
            if(!processCookie(fin)) return false;
            //Next header. We won't be saving this.
            continue;
        }

        //Set the header value
        incoming_headers[key] = fin;
    }

    return true;
}

bool io::HttpSession::processCookie(char* cookie)
{
    char* nck = cookie;
    while(*nck != '\0')
    {
        char* semic = nck;
        char* eq = nck;

        //Find the semicolon or end
        while(*semic != ';' && *semic != '\0') semic++;

        //Find the equal sign
        while(*eq != '=' && *eq != '\0') eq++;
        if(*eq == '\0') return false;

        //If the equal sign is after the semicolon, error
        if(eq > semic) return false;

        //Key: {nck}=>{eq - 1}
        //Value: {eq + 1}=>{semic - 1}
        std::string key(nck, eq - nck);
        std::string value(eq + 1, semic - (eq + 1));

        //Set the cookie
        incoming_cookies[key] = value;

        if(*semic == '\0') return true;
        //Increment the pointer
        nck = semic + 1;
    }

    return true;
}

//Checks if necessary headers are there
//Checks if we have a WebSocket handshake
bool io::HttpSession::validateHeaders()
{
    //Check if the location header has been set if HTTP 1.1+
    if(_httpMajor >= 1 && _httpMinor >= 1 || _httpMajor > 1)
    {
        //Check if the host header exists
        if(incoming_headers.find("host") == incoming_headers.end())
        {
            return false;
        }

        if(incoming_headers.find("connection") != incoming_headers.end())
        {
            //Copy the headers to prevent unnecessary modification
            std::string connection = incoming_headers["connection"];
            std::string upgrade = incoming_headers["upgrade"];

            //Lowercase things for easier comparison
            util::lowercase(connection);
            util::lowercase(upgrade);

            //Check if a websocket upgrade is intended
            if(connection == "upgrade" && upgrade == "websocket")
            {
                //Check if necessary headers are found
                bool flg = true;
                if(incoming_headers.find("sec-websocket-key") == incoming_headers.end())
                    flg = false;
                if(incoming_headers.find("sec-websocket-version") == incoming_headers.end())
                    flg = false;

                //Set the websocket flag and prepare stuff
                if(flg)
                {
                    _flags |= io::flag::WEBSOCKET;
                    //Lowercase now so not necessary later
                    util::lowercase(incoming_headers["connection"]);
                    util::lowercase(incoming_headers["upgrade"]);
                }
            }
        }
    }
    return true;
}

bool io::HttpSession::parsePost()
{
    return true;
}