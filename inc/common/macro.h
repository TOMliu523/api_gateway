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

#ifndef LIKEYLY
#define LIKEYLY(x) __builtin_expect(!!(x), 1)
#endif // LIKELY

#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif // UNLIKELY

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

#ifndef ARR_NUMS
#define ARR_NUMS(a) (sizeof(a) / sizeof(a[0]))
#endif // ARR_NUMS

#ifndef CAT
#define CAT(v1, v2) v1##v2
#endif // CAT

#ifndef CAT1
#define CAT1(v1, v2) CAT(v1, v2)
#endif

#ifndef CAT2
#define CAT2(v1, v2, v3) v1##v2##v3
#endif // CAT2

#endif // __MACRO_H__