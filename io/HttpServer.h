#ifndef _HTTP_SERVER_H
#define _HTTP_SERVER_H

#include "TcpServer.h"
#include <unordered_map>

namespace io
{
    namespace flag
    {
        extern uint32_t GET;
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

    class HttpServer;
    class HttpSession;
    class WsSession;
    class DataType;
    class HeaderField;

    class DataType
    {
        std::string data;
        std::string filepath;
    };

    class HeaderField
    {
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

        int _flags;
        unsigned char _httpMajor;
        unsigned char _httpMinor;

        bool parseGetLine();
        bool processPath(char* start, char* end);   //Arguments allow a space/op optimized implementation
        bool parseHeaders();
        bool processCookie(char* cookie);
        bool validateHeaders();
        bool parsePost();

        std::string path;
        std::string trimmedPath;

    public:
        std::unordered_map<std::string, std::string> get_queries;
        std::unordered_map<std::string, DataType> post_queries;
        std::unordered_map<std::string, std::string> incoming_headers;
        std::unordered_map<std::string, std::string> incoming_cookies;

        HttpSession();
        ~HttpSession();

        bool nextRequest();
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
    }
}

#endif