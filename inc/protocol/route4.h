/*****************************************************************************
 * filename: route4.h
 * function:
 * description:
 ****************************************************************************/

#pragma once

#include <stdint.h>

extern void *route4_thread_create(void ***, int);
extern void route4_thread_destroy(void *);
extern int route4_next_hop_get(uint32_t *, uint32_t);