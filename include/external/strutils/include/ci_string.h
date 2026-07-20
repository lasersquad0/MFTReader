/* 
*   Case INsensitive strings. 
* 
*  Compare, find, check for equality and other operations with these strings automatically are case insensitive. 
*/

#pragma once

#include <string>
#include <ostream>
#include "../include/string_utils.h"

template <class CH>
struct ci_char_traits : public std::char_traits<CH>
{
private:
    static inline char mytoupper(char c) { return (char)::toupper((int)c); }
    static inline wint_t mytoupper(wchar_t w) { return ::towupper((wint_t)w); }

public:
    static bool eq(CH c1, CH c2) { return mytoupper(c1) == mytoupper(c2); }
    static bool ne(CH c1, CH c2) { return mytoupper(c1) != mytoupper(c2); }
    static bool lt(CH c1, CH c2) { return mytoupper(c1) < mytoupper(c2); }

    static int compare(const CH* s1, const CH* s2, size_t n)
    {
        CH c1, c2;
        while (n-- != 0)
        {
            c1 = mytoupper(*s1);
            c2 = mytoupper(*s2);

            if (c1 < c2) return -1;
            if (c1 > c2) return 1;
            ++s1; ++s2;
        }
        return 0;
    }

    static const CH* find(const CH* s, size_t n, CH a)
    {
        CH A = (CH)mytoupper(a);
        for (size_t i = 0; i < n; i++)
        {
            if (mytoupper(*s) == A) return s;
            s++;
        }

        return nullptr;
    }
};


typedef std::basic_string<char, ci_char_traits<char>> ci_astring; // ANSI string
typedef std::basic_string<wchar_t, ci_char_traits<wchar_t>> ci_wstring; // Wide Char string

// define general string name - ci_string depending on whether project supports UNICODE or not
#ifdef UNICODE
typedef ci_wstring ci_string;
#else
typedef ci_astring ci_string;
#endif

inline void PrintTo(const ci_string& s, std::ostream* os)
{    
    *os << convert_string<char>(s);
}
