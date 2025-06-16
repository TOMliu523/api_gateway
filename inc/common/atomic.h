/************************************************
 * filename: atomic.h
 * function:
 * description:
 ***********************************************/

#ifndef __ATOMIC_H__
#define __ATOMIC_H__

#ifndef ATOMIC_RMB
#define ATOMIC_RMB() __atomic_thread_fence(__ATOMIC_ACQUIRE)
#endif // ATOMIC_RMB

#ifndef ATOMIC_WMB
#define ATOMIC_WMB() __atomic_thread_fence(__ATOMIC_RELEASE)
#endif // ATOMIC_WMB

#ifndef ATOMIC_BARRIER
#define ATOMIC_BARRIER() __atomic_thread_fence(__ATOMIC_ACQ_REL)
#endif // ATOMIC_BARRIER

#ifndef ATOMIC_ADD_FETCH
#define ATOMIC_ADD_FETCH(ptr, val) __atomic_add_fetch(ptr, val, __ATOMIC_ACQ_REL)
#endif // ATOMIC_ADD_FETCH

#ifndef ATOMIC_SUB_FETCH
#define ATOMIC_SUB_FETCH(ptr, val) __atomic_sub_fetch(ptr, val, __ATOMIC_ACQ_REL)
#endif // ATOMIC_SUB_FETCH

#ifndef ATOMIC_AND_FETCH
#define ATOMIC_AND_FETCH(ptr, val) __atomic_and_fetch(ptr, val, __ATOMIC_ACQ_REL)
#endif // ATOMIC_AND_FETCH

#ifndef ATOMIC_XOR_FETCH
#define ATOMIC_XOR_FETCH(ptr, val) __atomic_xor_fetch(ptr, val, __ATOMIC_ACQ_REL)
#endif // ATOMIC_XOR_FETCH

#ifndef ATOMIC_OR_FETCH
#define ATOMIC_OR_FETCH(ptr, val) __atomic_or_fetch(ptr, val, __ATOMIC_ACQ_REL)
#endif // ATOMIC_OR_FETCH

#ifndef ATOMIC_NAND_FETCH
#define ATOMIC_NAND_FETCH(ptr, val) __atomic_nand_fetch(ptr, val, __ATOMIC_ACQ_REL)
#endif // ATOMIC_NAND_FETCH

#ifndef ATOMIC_FETCH_ADD
#define ATOMIC_FETCH_ADD(ptr, val) __atomic_fetch_add(ptr, val, __ATOMIC_ACQ_REL)
#endif // ATOMIC_FETCH_ADD

#ifndef ATOMIC_FETCH_SUB
#define ATOMIC_FETCH_SUB(ptr, val) __atomic_fetch_sub(ptr, val, __ATOMIC_ACQ_REL)
#endif // ATOMIC_FETCH_SUB

#ifndef ATOMIC_FETCH_AND
#define ATOMIC_FETCH_AND(ptr, val) __atomic_fetch_and(ptr, val, __ATOMIC_ACQ_REL)
#endif // ATOMIC_FETCH_AND

#ifndef ATOMIC_FETCH_XOR
#define ATOMIC_FETCH_XOR(ptr, val) __atomic_fetch_xor(ptr, val, __ATOMIC_ACQ_REL)
#endif // ATOMIC_FETCH_XOR

#ifndef ATOMIC_FETCH_OR
#define ATOMIC_FETCH_OR(ptr, val) __atomic_fetch_or(ptr, val, __ATOMIC_ACQ_REL)
#endif // ATOMIC_FETCH_OR

#ifndef ATOMIC_FETCH_NAND
#define ATOMIC_FETCH_NAND(ptr, val) __atomic_fetch_nand(ptr, val, __ATOMIC_ACQ_REL)
#endif // ATOMIC_FETCH_NAND

#endif // __ATOMIC_H__