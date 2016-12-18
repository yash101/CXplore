#include "HttpServer.h"

uint32_t io::flag::GET = 0x00000001;
uint32_t io::flag::POST = 0x00000002;
uint32_t io::flag::PUT = 0x00000004;
uint32_t io::flag::DELETE = 0x00000008;
uint32_t io::flag::OPTIONS = 0x00000010;
uint32_t io::flag::CONNECT = 0x00000020;

uint32_t io::flag::WEBSOCKET = 0x00000040;
uint32_t io::flag::KEEPALIVE = 0x00000080;