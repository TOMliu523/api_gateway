/*****************************************************************************
 * filename: common.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __COMMON_H__
#define __COMMON_H__

#include <stddef.h>

extern void check_and_wait(void *array[], int count);
extern int alloc_align(void **, size_t);

#endif // __COMMON_H__
