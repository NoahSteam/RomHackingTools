#pragma once
#if !defined(_WIN32) && !defined(_WIN64)
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <limits.h>

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif
#ifndef MAX_PATH
#define MAX_PATH PATH_MAX
#endif
using byte  = unsigned char;
using BYTE  = unsigned char;
using UCHAR = unsigned char;
using WORD  = unsigned short;
using DWORD = unsigned long;

using errno_t = int;

inline int CopyFile(const char* src, const char* dst, int bFailIfExists) {
  namespace fs = std::filesystem;
  std::error_code ec;
  auto opts = bFailIfExists ? fs::copy_options::none
                            : fs::copy_options::overwrite_existing;
  fs::copy_file(fs::path(src), fs::path(dst), opts, ec);
  return ec ? 0 : 1;  // match WinAPI: nonzero=success, 0=failure
}

inline char* strtok_s(char* str, const char* delim, char** context) {
  return strtok_r(str, delim, context);
}

inline int sprintf_s(char* buffer, size_t size, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = std::vsnprintf(buffer, size, fmt, args);
  va_end(args);
  return result;
}

inline int fscanf_s(FILE* stream, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  int result = std::vfscanf(stream, fmt, args);
  va_end(args);
  return result;
}

inline errno_t memcpy_s(void* dst, size_t dstsz, const void* src, size_t count) {
  if (count > dstsz) return EINVAL;   // mimic bounds check
  std::memcpy(dst, src, count);
  return 0;
}

static errno_t fopen_s(FILE **f, const char *name, const char *mode) {
  if (!f) return EINVAL;
  FILE *tmp = fopen(name, mode);
  if (!tmp) { *f = NULL; return errno; }
  *f = tmp;
  return 0;
}

inline errno_t fread_s(void* buffer, size_t bufferSize, size_t elementSize, size_t count, FILE* stream)
{
  if (!buffer || !stream) return EINVAL;
  size_t readCount = fread(buffer, elementSize, count, stream);
  // Optional sanity check:
  if (readCount > bufferSize)
  if (readCount > count) return EINVAL;
  return 0;  // mimic success
}

// shim_mbstowcs_s.h
// POSIX-compatible shim for MSVC's mbstowcs_s.
// Semantics: if dst==nullptr or size==0, returns required count (incl. NUL) via outCount.
inline errno_t mbstowcs_s(std::size_t* outCount,
                          wchar_t* dst, std::size_t size,   // size in wchar_t units
                          const char* src, std::size_t /*srcCount*/) {
  mbstate_t st{}; 
  const char* p = src;

  if (!dst || size == 0) {
    std::size_t n = mbsrtowcs(nullptr, &p, 0, &st);     // chars needed (excl. NUL)
    if (n == (std::size_t)-1) return EILSEQ;
    if (outCount) *outCount = n + 1;                    // include NUL
    return 0;
  }

  std::size_t n = mbsrtowcs(dst, &p, size - 1, &st);    // leave room for NUL
  if (n == (std::size_t)-1) return EILSEQ;
  dst[n] = L'\0';
  if (outCount) *outCount = n + 1;
  return 0;
}


#endif
