#include "io/io.h"
#include <thread>
#include <stdio.h>
#include "io/HttpServer.h"
int main(int argc, char** argv)
{
    io::TcpServer server;
    server.addListeningPort(1234);
    server.addListeningPort(1235);
    server.addListeningPort(1236);
    server.setMaxNumberConnectedClients(0);
    server.setTcpConnectionQueueSize(3);
    server.setTimeout(0, 0);

    server.startServer();
    io::HttpServer hsrv(&server);

    while(true)
    {
        io::HttpSession* sess = hsrv();
        while(sess->nextRequest())
        {
            sess->response.data = "Hello World!";
            sess->response.mime_type = "text/html";
            sess->sendResponse();
        }
        delete sess;
    }
}