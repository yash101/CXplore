#include "stringproc.h"
#include <ctype.h>
#include <string.h>

void util::lowercase(char* str)
{
    while(*str != '\0')
    {
        *str = tolower(*str);
        str++;
    }
}

void util::uppercase(char* str)
{
    while(*str != '\0')
    {
        *str = toupper(*str);
        str++;
    }
}

void util::lowercase(std::string& str)
{
    for(size_t i = 0; i < str.size(); i++)
    {
        str[i] = tolower(str[i]);
    }
}

void util::uppercase(std::string& str)
{
    for(size_t i = 0; i < str.size(); i++)
    {
        str[i] = toupper(str[i]);
    }
}
