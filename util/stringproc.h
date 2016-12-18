#ifndef _STRINGPROC_H
#define _STRINGPROC_H
#include <string>

namespace util
{
    void lowercase(char* str);
    void uppercase(char* str);
    void lowercase(std::string& str);
    void uppercase(std::string& str);
}

#endif