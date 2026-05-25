/* platform.h — Cross-platform compatibility shims for the zc compiler
   Supported targets:
     - macOS  (x86_64, arm64)   — clang or Homebrew gcc
     - Linux  (x86_64, arm64)   — gcc or clang
     - Windows (x86_64)         — MinGW/MSYS2 or LLVM clang (MSVC)
*/
#ifndef ZC_PLATFORM_H
#define ZC_PLATFORM_H

#include <stdlib.h>   /* getenv()        */
#include <string.h>   /* strlen, memcpy  */

/* ── Windows (MinGW / MSVC) ──────────────────────────────────────── */
#ifdef _WIN32
/*
 * We intentionally do NOT include <windows.h> here.
 * winnt.h (pulled in by windows.h) defines a global enum value named
 * `TokenType` inside TOKEN_INFORMATION_CLASS, which collides with our
 * own `TokenType` typedef in lexer.h.  Since we only need the temp
 * directory and a process ID, we use pure-C alternatives instead:
 *   - getenv("TEMP") / getenv("TMP")  →  no windows.h needed
 *   - _getpid() from <process.h>      →  no windows.h needed
 */
#  include <process.h>   /* _getpid() */

   static inline const char *zc_tmpdir(void) {
       static char buf[512];
       const char *tmp = getenv("TEMP");
       if (!tmp) tmp = getenv("TMP");
       if (!tmp) tmp = "C:\\Temp";
       size_t n = strlen(tmp);
       if (n >= sizeof(buf) - 2) n = sizeof(buf) - 2;
       memcpy(buf, tmp, n);
       if (n > 0 && buf[n-1] != '\\' && buf[n-1] != '/') buf[n++] = '\\';
       buf[n] = '\0';
       return buf;
   }

#  define ZC_GETPID()    ((int)_getpid())
#  define ZC_EXE_SUFFIX  ".exe"

/* ── Unix (macOS / Linux) ────────────────────────────────────────── */
#else
#  include <unistd.h>

   static inline const char *zc_tmpdir(void) { return "/tmp/"; }

#  define ZC_GETPID()    ((int)getpid())
#  define ZC_EXE_SUFFIX  ""
#endif

#endif /* ZC_PLATFORM_H */
