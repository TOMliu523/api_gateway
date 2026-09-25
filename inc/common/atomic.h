/************************************************
 * filename: atomic.h
 * function:
 * description:
 ***********************************************/

#ifndef __ATOMIC_H__
#define __ATOMIC_H__

#include <stdatomic.h>

#ifdef __x86_64
#ifndef atomic_prevent_compiler_reorder
#define atomic_prevent_compiler_reorder() __asm__ __volatile__ ("" ::: "memory");
#endif // atomic_prevent_compiler_reorder
#elif defined(__aarch64__)
#define atomic_prevent_compiler_reorder() __asm__ __volatile__ ("dmb ish" ::: "memory")
#else
#error "Unsupported architecture"
#endif

#ifndef atomic_rmb
#define atomic_rmb() __atomic_thread_fence(memory_order_acquire)
#endif // atomic_rmb

#ifndef atomic_wmb
#define atomic_wmb() __atomic_thread_fence(memory_order_release)
#endif // atomic_wmb

#ifndef atomic_barrier
#define atomic_barrier() __atomic_thread_fence(memory_order_acq_rel)
#endif // atomic_barrier

#ifndef atomic_load_relaxed
#define atomic_load_relaxed(p) atomic_load_explicit(p, memory_order_relaxed)
#endif // atomic_load_relaxed

#ifndef atomic_store_relaxed
#define atomic_store_relaxed(p, v) atomic_store_explicit(p, v, memory_order_relaxed)
#endif // atomic_store_relaxed

#endif // __ATOMIC_H__