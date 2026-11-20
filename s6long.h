/***************************************************************************************************
S6LONG.H - makes `long` mean 32 bits again (Linux port, step 3)

PORT NOTICE: this file is NOT part of Ken Silverman's original SLAB6 release. SLAB6 was written for
32-bit Windows, where `long` is 32 bits. It uses `long` for file-format fields, bit arrays, float bit
tricks (*(long *)&f), 32-bit wraparound arithmetic, and so on. On 64-bit Linux `long` is 64 bits and
all of that breaks silently. Rather than edit thousands of declarations, this header redefines the
keyword so the code means exactly what it meant on Win32. (On MSVC it is not used: there it is a no-op.)

Consequences, all handled in the port:
  - a `long` can no longer hold a pointer: use intptr_t for that (see tiletype.f in slab6.c)
  - printf formats must be %d, not %ld
  - every system header must be included BEFORE this one (they are pre-included below, so a later
    #include of the same header is harmless), and so must third-party headers such as SDL.h
Credit for SLAB6 belongs to Ken Silverman.
***************************************************************************************************/
#ifndef S6LONG_H
#define S6LONG_H

#ifndef _MSC_VER
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <math.h>
#include <fenv.h>
#include <limits.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <fnmatch.h>
#define long int

	//Proof that the remap took effect and that intptr_t still holds a pointer
typedef char s6_assert_long_is_32bit[(sizeof(long) == 4) ? 1 : -1];
typedef char s6_assert_intptr_holds_pointer[(sizeof(intptr_t) == sizeof(void *)) ? 1 : -1];
#endif

#endif
