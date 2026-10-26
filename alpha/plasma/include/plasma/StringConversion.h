#pragma once
#include <string>

// Shared narrow/wide conversion helpers used by the scene and font readers.
std::wstring widenString(const std::string &text);
std::string narrowString(const std::wstring &text);

// The file name the standard streams take: the wide path itself on Windows; elsewhere its bytes, with
// '/' for '\\' (the game's paths are ASCII).
#ifdef _WIN32
inline const wchar_t *streamPath(const std::wstring &path)
{
    return path.c_str();
}
#else
inline std::string streamPath(const std::wstring &path)
{
    std::string result = narrowString(path);
    for (char &c : result)
        if (c == '\\')
            c = '/';
    return result;
}
#endif
