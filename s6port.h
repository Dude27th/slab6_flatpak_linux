/***************************************************************************************************
S6PORT.H - portable C replacements for SLAB6's MSVC inline assembly (Linux port, step 2)

PORT NOTICE: this file is NOT part of Ken Silverman's original SLAB6 release. It replaces, with
plain C, the small _asm helpers that slab6.c only defines when compiled with MSVC (_MSC_VER).
Credit for SLAB6 belongs to Ken Silverman. License of the original: no commercial exploitation,
keep the author's name/credit, and make clear that modified code is modified.

Every helper keeps the name, arguments and 32-bit semantics of the assembly it replaces.

Build slab6.c with:  -fno-strict-aliasing -frounding-math
  (the code type-puns floats through long*, and relies on changing the FPU rounding mode)
***************************************************************************************************/
#ifndef S6PORT_H
#define S6PORT_H

#include "s6long.h" //(must come first: long == 32 bits)
#include <string.h>
#include <math.h>
#include <fenv.h>
#include <stdint.h>
#include <stdio.h>
#include <strings.h>

#define _inline inline
#define __forceinline inline
#define __int64 int64_t

	//rep stosb: fill c bytes at d with the low byte of a (nothing if c <= 0)
static inline void clearbufbyte (void *d, long c, long a)
{
	if (c > 0) memset(d,(int)(a&255),(size_t)c);
}

	//x87 control word -> C99 rounding mode. SLAB6 only ever does
	//  fldcw((fstcw()&~0xf00)|0x800)   "24-bit precision, round up"      (rendering)
	//  fldcw((fstcw()&~0xf00)|0x200)   "53-bit precision, round nearest" (init)
	//so only the rounding-control bits (10-11) are translated. There is no precision-control
	//equivalent for SSE: float arithmetic is already 24-bit.
static inline long fstcw ()
{
	long rc;
	switch (fegetround())
	{
		case FE_DOWNWARD: rc = 0x400; break;
		case FE_UPWARD: rc = 0x800; break;
		case FE_TOWARDZERO: rc = 0xc00; break;
		default: rc = 0; break;
	}
	return(0x037f|rc); //0x037f = x87 default control word
}

static inline void fldcw (long fpumode)
{
	switch (fpumode&0xc00)
	{
		case 0x400: fesetround(FE_DOWNWARD); break;
		case 0x800: fesetround(FE_UPWARD); break;
		case 0xc00: fesetround(FE_TOWARDZERO); break;
		default: fesetround(FE_TONEAREST); break;
	}
}

	//fld f / fistp: float -> integer using the CURRENT rounding mode (lrintf does the same)
static inline void ftol (float f, long *a)
{
	(*a) = (long)lrintf(f);
}

static inline void fcossin (float a, float *c, float *s)
{
	(*c) = cosf(a); (*s) = sinf(a);
}

	//a[i] = (d + i*s) >> 16 for i=0..c-1, in 32-bit signed arithmetic
static inline void qinterpolatedown16 (long *a, long c, long d, long s)
{
	long i;
	int32_t dd = (int32_t)d;
	for(i=0;i<c;i++) { a[i] = (long)(dd>>16); dd = (int32_t)((uint32_t)dd + (uint32_t)s); }
}

	//a[i] += (d + i*s) & 0xffff0000 for i=0..c-1 (unused by SLAB6, kept for completeness)
static inline void qinterpolatehiadd16 (long *a, long c, long d, long s)
{
	long i;
	uint32_t dd = (uint32_t)d;
	for(i=0;i<c;i++) { a[i] += (long)(int32_t)(dd&0xffff0000u); dd += (uint32_t)s; }
}

	//low 32 bits of a*d + s*t
static inline long dmulshr0 (long a, long d, long s, long t)
{
	return((long)(int32_t)((uint32_t)a*(uint32_t)d + (uint32_t)s*(uint32_t)t));
}

	//bit scan forward / reverse on the low 32 bits (0 if there is no bit set; the CPU leaves it undefined)
static inline long bsf (long a)
{
	uint32_t u = (uint32_t)a;
	return(u ? (long)__builtin_ctz(u) : 0);
}
static inline long bsr (long a)
{
	uint32_t u = (uint32_t)a;
	return(u ? (long)(31-__builtin_clz(u)) : 0);
}

	//Other MSVC-only names slab6.c uses
#define _vsnprintf vsnprintf
#define stricmp strcasecmp

	//32-bit rotates (the code rotates a one-bit mask inside a 32-bit vbit[] word)
static inline unsigned long _lrotl (unsigned long v, int n)
{
	uint32_t u = (uint32_t)v; n &= 31;
	return((unsigned long)(n ? (uint32_t)((u<<n)|(u>>(32-n))) : u));
}
static inline unsigned long _lrotr (unsigned long v, int n)
{
	uint32_t u = (uint32_t)v; n &= 31;
	return((unsigned long)(n ? (uint32_t)((u>>n)|(u<<(32-n))) : u));
}


	//Unaligned 32-bit load (SLAB6 reads bit arrays at arbitrary byte offsets; fine on x86, but formally UB)
static inline long s6_load32 (const void *p)
{
	int32_t v; memcpy(&v,p,4); return((long)v);
}

#endif
