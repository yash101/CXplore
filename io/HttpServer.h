#ifndef _HTTP_SERVER_H
#define _HTTP_SERVER_H

#include "TcpServer.h"
#include <unordered_map>

namespace io
{
    //Various flags used
    namespace flag
    {
        extern uint32_t GET;
        extern uint32_t HEAD;
        extern uint32_t POST;
        extern uint32_t PUT;
        extern uint32_t DELETE;
        extern uint32_t OPTIONS;
        extern uint32_t CONNECT;

        extern uint32_t WEBSOCKET;
        extern uint32_t KEEPALIVE;

        extern uint32_t COOKIE_EXPIRES;
        extern uint32_t COOKIE_SECURE;
        extern uint32_t COOKIE_HTTPONLY;
        extern uint32_t COOKIE_SAMESITE;
    }

    //Classes defined in this header
    class HttpServer;
    class HttpSession;
    class WsSession;
    class DataType;
    class HeaderField;
    class Cookie;

    //Holds either string data or file path
    class DataType
    {
    public: 
        std::string data;
        std::string filepath;
        std::string mime_type;
    };

    //Holds a cookie
    class Cookie
    {
    public:
        uint32_t flags;
        void clearFlag(uint32_t flg);
        void setFlag(uint32_t flg);
        bool getFlag(uint32_t flg);
        
        std::string name;
        std::string value;
        std::string expires;
    };

    //Holds an HTTP header
    class HeaderField
    {
    public:
        std::string name;
        std::string content;
    };

    //Serves stuff
    class HttpServer
    {
        friend class HttpSession;
        friend class WsSession;
    private:
    protected:
        //The TCP server served from
        io::TcpServer* _server;
        //Various limits
        size_t _maxGetLineLength,
            _maxHeaderLength,
            _maxHeaderCount,
            _maxHeaderNameLength,
            _maxHeaderFieldLength,
            _maxPostSize;
        
        //Initializes misc. variables
        void initializeVariables();
    public:
        //Constructors
        HttpServer();
        HttpServer(io::TcpServer* server);

        //Access to the internal io::TcpServer pointer
        io::TcpServer* setTcpServer(io::TcpServer* newServer);
        io::TcpServer* getTcpServer();

        //Next connection
        HttpSession* operator()();
    };

    //Allows interaction with the client
    class HttpSession
    {
        friend class HttpServer;
        friend class WsSession;
    protected:
        io::TcpServerConnection* _connection;
        io::HttpServer* _httpServer;
        io::TcpServer* _tcpServer;

        WsSession* _wssession;

        size_t _reqct;
        bool _contReq;
        bool _requestPending;

        int _flags;
        unsigned char _httpMajor;
        unsigned char _httpMinor;

        bool parseGetLine();
        bool processPath(char* start, char* end);   //Arguments allow a space/op optimized implementation
        bool parseHeaders();
        bool processCookie(char* cookie);
        bool validateHeaders();
        bool parsePost();
        bool checkHeaders();
        bool _sendResponse();

    public:
        //Maps containing data pulled from the request
        std::unordered_map<std::string, std::string> get_queries;
        std::unordered_map<std::string, DataType> post_queries;
        std::unordered_map<std::string, std::string> incoming_headers;
        std::unordered_map<std::string, std::string> incoming_cookies;
        std::unordered_map<std::string, std::string> outgoing_headers;
        std::unordered_map<std::string, io::Cookie> outgoing_cookies;

        //Paths from the client
        std::string fullPath;
        std::string path;

        unsigned short int status_code;
        std::string status_string;
        io::DataType response;

        HttpSession();
        ~HttpSession();

        bool nextRequest();
        bool sendResponse();
        bool isFlagSet(uint32_t flag);
    };

    //Interface to allow websocket-centered applications
    class WsSession
    {
        friend class HttpServer;
        friend class HttpSession;
    private:
    protected:
    public:
    };

    namespace http
    {
        void capitalize(char* str);
        void capitalize(std::string& str);
        std::string getDate();
    }

    void initializeHttpMethodResolver();
    void initializeHttpStatusCodeResolver();
    void initializeHttpResolvers();
}

#endif