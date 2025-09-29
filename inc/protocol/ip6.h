/*****************************************************************************
 * filename: ip6.h
 * function:
 * description:
 *****************************************************************************/

#pragma once

#include "l3.h"

extern void ip6_process(void **, int);
extern void ip6_manage_destroy(void *);

extern void *ip6_manage_startup(int, int);
extern void *ip6_thread_ndp_table_create(int, int, int);
extern void ip6_thread_ndp_table_destroy(void *);

extern void ip6_ndp_refresh(void);
extern void ip6_ndp_update_or_create(void *);