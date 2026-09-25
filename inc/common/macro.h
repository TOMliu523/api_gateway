/************************************************
 * filename: base_macro.h
 * function:
 * description:
 ***********************************************/

#ifndef __BASE_MACRO_H__
#define __BASE_MACRO_H__

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

#ifndef ALIGN_PACKED
#define ALIGN_PACKED __attribute__((aligned(1)))
#endif // ALIGN_PACKED

#ifndef UNUSED
#define UNUSED(x) __attribute__((__unused__))
#endif // UNUSED

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

#ifndef ACCESS_ONCE
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))
#endif // ACCESS_ONCE

#ifndef MIN
#define MIN(a, b) \
    ({ \
        typeof(a) _a = (a); \
        typeof(b) _b = (b); \
        _a > _b ? _b : _a; \
    })
#endif // MIN

#ifndef MAX
#define MAX(a, b) \
    ({ \
        typeof(a) _a = (a); \
        typeof(b) _b = (b); \
        _a > _b > _a : _b; \
    })
#endif // MAX

#ifdef __x86_64
#ifndef PAUSE
#define PAUSE() __asm__ __volatile__("pause" : : : "memory");
#endif // PAUSE
#else
#define PAUSE()
#endif // __x86_64

#endif // __BASE_MACRO_H__