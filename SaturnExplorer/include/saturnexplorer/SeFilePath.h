/* Saturn Explorer -- opening a file by a UTF-8 path.
 *
 * Every path the program holds is UTF-8 (WinMain builds the command line from __wargv, the file dialogs
 * return UTF-8). On Linux and macOS a char* path is passed to the kernel as it is. On Windows the CRT's
 * fopen, _stat64, _open and std::ifstream(const char*) read a char* in the ANSI code page, so a path with a
 * character outside it (a Japanese folder name on an English system) does not open, and the failure looks
 * like a missing file (A12-25). These helpers convert to UTF-16 and call the wide form on Windows; on other
 * systems they are the plain call, so a path behaves exactly as before there.
 *
 * A path that is not valid UTF-8 (an ANSI-coded name from an old settings file) goes to the ANSI fopen in
 * OpenFile, as before; the other helpers widen its bytes one for one, which is exact for ASCII.
 *
 * Header-only, C++14, no dependency beyond the C library (and <cwchar> on Windows).
 */
#ifndef SATURNEXPLORER_SE_FILE_PATH_H
#define SATURNEXPLORER_SE_FILE_PATH_H

#ifdef __cplusplus

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <cwchar>   // _wfopen
#endif

namespace se
{

// UTF-8 to UTF-16, written out here (and built on every platform, so the tests reach them) so that this header (included from many files) does not pull <windows.h> and its macros in.
// Returns false for text that is not valid UTF-8; 'out' is then unspecified.
inline bool TryUtf8ToWide(const std::string& s, std::wstring& out)
{
    out.clear();
    const size_t n = s.size();
    for (size_t i = 0; i < n;)
    {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        unsigned long cp;
        size_t extra;
        if (c < 0x80) { cp = c; extra = 0; }
        else if (c >= 0xC2 && c <= 0xDF) { cp = c & 0x1Fu; extra = 1; }
        else if (c >= 0xE0 && c <= 0xEF) { cp = c & 0x0Fu; extra = 2; }
        else if (c >= 0xF0 && c <= 0xF4) { cp = c & 0x07u; extra = 3; }
        else return false;
        if (i + extra >= n) return false;   // truncated sequence
        for (size_t k = 1; k <= extra; ++k)
        {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        if ((extra == 2 && cp < 0x800) || (extra == 3 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (cp >= 0x10000)
        {
            cp -= 0x10000;
            out += static_cast<wchar_t>(0xD800 + (cp >> 10));
            out += static_cast<wchar_t>(0xDC00 + (cp & 0x3FF));
        }
        else out += static_cast<wchar_t>(cp);
        i += extra + 1;
    }
    return true;
}

// UTF-8 to UTF-16. Text that is not valid UTF-8 (an old ANSI-coded name) is widened byte for byte, which is exact for ASCII.
inline std::wstring Utf8ToWide(const std::string& s)
{
    std::wstring w;
    if (TryUtf8ToWide(s, w)) return w;
    w.clear();
    for (size_t i = 0; i < s.size(); ++i) w += static_cast<wchar_t>(static_cast<unsigned char>(s[i]));
    return w;
}

// UTF-16 to UTF-8, for names a directory listing returns.
inline std::string WideToUtf8(const std::wstring& w)
{
    std::string out;
    for (size_t i = 0; i < w.size(); ++i)
    {
        unsigned long cp = static_cast<unsigned short>(w[i]);
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < w.size() &&
            static_cast<unsigned short>(w[i + 1]) >= 0xDC00 && static_cast<unsigned short>(w[i + 1]) <= 0xDFFF)
        {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (static_cast<unsigned short>(w[++i]) - 0xDC00);
        }
        if (cp < 0x80) out += static_cast<char>(cp);
        else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    }
    return out;
}

#ifdef _WIN32

// The form an OS call or a stream constructor takes: UTF-16 here. Use as `std::ifstream f(se::NativePath(p).c_str())`
// (the temporary lives to the end of the full expression, which covers the constructor).
inline std::wstring NativePath(const std::string& utf8) { return Utf8ToWide(utf8); }

inline std::FILE* OpenFile(const std::string& utf8Path, const char* mode)
{
    std::wstring wide;
    // Not valid UTF-8: the ANSI call, as before this header existed.
    if (!TryUtf8ToWide(utf8Path, wide)) return std::fopen(utf8Path.c_str(), mode);
    std::wstring wmode;
    for (const char* m = mode; *m; ++m) wmode += static_cast<wchar_t>(static_cast<unsigned char>(*m));
    return ::_wfopen(wide.c_str(), wmode.c_str());
}

#else

inline const std::string& NativePath(const std::string& utf8) { return utf8; }

inline std::FILE* OpenFile(const std::string& utf8Path, const char* mode)
{
    return std::fopen(utf8Path.c_str(), mode);
}

#endif

}  // namespace se

#endif  // __cplusplus
#endif  // SATURNEXPLORER_SE_FILE_PATH_H
