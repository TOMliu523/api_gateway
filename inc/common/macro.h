/************************************************
 * filename: macro.h
 * function:
 * description:
 ***********************************************/

#ifndef __MACRO_H__
#define __MACRO_H__

#ifndef CACHE_LINE
#define CACHE_LINE 64
#endif // CACHE_LINE

#ifndef INLINE
#define INLINE inline __attribute__((always_inline))
#endif // INLINE

#ifndef FALLTHROUGH
#define FALLTHROUGH __attribute__((fallthrough))
#endif // FALLTHROUGH

#ifndef PROC_INIT
#define PROC_INIT __attribute__((constructor))
#endif // PROC_INIT

#ifndef PROC_FINI
#define PROC_FINI __attribute__((destructor))
#endif // PROC_FINI

#ifndef WEAK
#define WEAK __attribute__((weak))
#endif // WEAK

#ifndef ALIGNED
#define ALIGNED(n) __attribute__((aligned(n)))
#endif // ALIGNED

#endif // __MACRO_H__