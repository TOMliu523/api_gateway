/*****************************************************************************
 * filename: ip6.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __IP6_H__
#define __IP6_H__

#include "l3.h"

extern void ip6_process(void **, int);
extern void ip6_destroy(void *);

extern void *ip6_startup(int, int);
extern void *ip6_thread_ndp_table_create(int, int, int);
extern void ip6_thread_ndp_table_destroy(void *);

extern void ip6_ndp_refresh(void);
extern void ip6_ndp_update_or_create(void *);

#endif // __IP6_H__