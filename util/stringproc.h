#ifndef _STRINGPROC_H
#define _STRINGPROC_H
#include <string>
#include <sstream>

namespace util
{
    void lowercase(char* str);
    void uppercase(char* str);
    void lowercase(std::string& str);
    void uppercase(std::string& str);

    template<typename T>
    std::string toString(T x)
    {
        std::stringstream str;
        str << x;
        return str.str();
    }
}

#endif