/* platform.h — Cross-platform compatibility shims for the zc compiler
   Supported targets:
     - macOS  (x86_64, arm64)   — clang or Homebrew gcc
     - Linux  (x86_64, arm64)   — gcc or clang
     - Windows (x86_64)         — MinGW/MSYS2 or LLVM clang
*/
#ifndef ZC_PLATFORM_H
#define ZC_PLATFORM_H

/* ── Windows (MinGW / MSVC) ──────────────────────────────────────── */
#ifdef _WIN32
#  include <windows.h>
#  include <process.h>   /* _getpid() */

   /* Temporary directory with trailing separator */
   static inline const char *zc_tmpdir(void) {
       static char buf[MAX_PATH + 2];
       DWORD n = GetTempPathA(MAX_PATH, buf);
       if (n == 0) { return "C:\\Temp\\"; }
       /* ensure trailing backslash */
       if (buf[n - 1] != '\\' && buf[n - 1] != '/') {
           buf[n] = '\\'; buf[n + 1] = '\0';
       }
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
