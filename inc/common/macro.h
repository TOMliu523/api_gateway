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

#ifndef LIKELY
#define LIKELY(x) __builtin_expect(!!(x), 1)
#endif // LIKELY

#ifndef UNLIKELY
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#endif // UNLIKELY

#ifndef INLINE
#define INLINE inline __attribute__((always_inline))
#endif // INLINE

#ifndef FALLTHROUGH
# if defined(__GNUC__) && __GNUC__ >= 7
#  define FALLTHROUGH __attribute__((fallthrough))
# else
#  define FALLTHROUGH ((void)0)
# endif
#endif

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
#define ALIGN_PACKED __attribute__((__packed__))
#endif // ALIGN_PACKED

#ifndef ALIGN_CACHE_LINE
#define ALIGN_CACHE_LINE __attribute__((aligned(CACHE_LINE)))
#endif // ALIGN_CACHE_LINE

#ifndef ALIGN_PACKED_CACHE_LINE
#define ALIGN_PACKED_CACHE_LINE __attribute__((__packed__, aligned(CACHE_LINE)))
#endif // ALIGN_PACKED_CACHE_LINE

#ifndef UNUSED
#define UNUSED __attribute__((__unused__))
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
#define CAT2(v1, v2, v3) CAT1(CAT1(v1, v2), v3)
#endif // CAT2

#ifndef ACCESS_ONCE
#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))
#endif // ACCESS_ONCE

#define SWAP(a, b) \
    do { \
        typeof(a) _tmp = (a); \
        (a) = (b); \
        (b) = _tmp; \
    } while (0)

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
        _a > _b ? _a : _b; \
    })
#endif // MAX

#ifndef UNROLL_LOOP_8
#define UNROLL_LOOP_8(__idx, _count, _body)                      \
    do {                                                         \
        typeof(_count) __idx = 0;                                \
        typeof(_count) __n = (_count);                           \
        switch (__n % 8) {                                       \
        case 0: do { _body; __idx++;                             \
                    FALLTHROUGH;                                 \
        case 7: _body; __idx++;                                  \
                    FALLTHROUGH;                                 \
        case 6: _body; __idx++;                                  \
                    FALLTHROUGH;                                 \
        case 5: _body; __idx++;                                  \
                    FALLTHROUGH;                                 \
        case 4: _body; __idx++;                                  \
                    FALLTHROUGH;                                 \
        case 3: _body; __idx++;                                  \
                    FALLTHROUGH;                                 \
        case 2: _body; __idx++;                                  \
                    FALLTHROUGH;                                 \
        case 1: _body; __idx++;                                  \
            } while (__idx < __n);                               \
        }                                                        \
    } while (0)
#endif // UNROLL_LOOP_8

#ifndef UNROLL_LOOP_4
#define UNROLL_LOOP_4(__idx, _count, _body)                      \
    do {                                                         \
        typeof(_count) __idx = 0;                                \
        typeof(_count) __n = (_count);                           \
        switch (__n % 4) {                                       \
        case 0: do { _body; __idx++;                             \
                  FALLTHROUGH;                                   \
        case 3: _body; __idx++;                                  \
                  FALLTHROUGH;                                   \
        case 2: _body; __idx++;                                  \
                  FALLTHROUGH;                                   \
        case 1: _body; __idx++;                                  \
            } while (__idx < __n);                               \
        }                                                        \
    } while (0)
#endif // UNROLL_LOOP_4

#define UNROLL_LOOP_N(__i, _count, _factor, _body)                       \
    do {                                                                 \
        typeof(_count) __i = 0;                                          \
        typeof(_count) __n = (_count);                                   \
        for (; __i + (_factor - 1) < __n; __i += (_factor)) {            \
            for (int __j = 0; __j < (_factor); ++__j) {                  \
                do { _body; } while (0);                                 \
            }                                                            \
        }                                                                \
        for (; __i < __n; ++__i) {                                       \
            do { _body; } while (0);                                     \
        }                                                                \
    } while (0)

#ifdef __x86_64
#ifndef PAUSE
#define PAUSE() __asm__ __volatile__("pause" : : : "memory");
#endif // PAUSE
#else
#define PAUSE()
#endif // __x86_64

#endif // __BASE_MACRO_H__