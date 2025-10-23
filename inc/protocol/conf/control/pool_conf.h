/*****************************************************************************
 * filename: pool_conf.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __POOL_CONF_H__
#define __POOL_CONF_H__

#include <stdbool.h>

#include "pool.h"

extern void pool_conf_free(struct pool *);
extern void pool_conf_table_destroy(void *);
extern int pool_conf_get_by_name(void *, struct pool **, const char *);
extern int pool_conf_get_by_id(void *, struct pool **, uint32_t);
extern struct pool *pool_conf_create(const char *, enum RS_SELECT_ALGO, int, int);
extern int pool_conf_table_add(void **, const void *, struct pool **, int, int);
extern int pool_conf_table_del(void **, const void *, struct pool **, int, int);
extern int pool_conf_get_count(const void *, int *);
int pool_conf_get_all(const void *, struct pool *[], int);

#endif // __POOL_CONF_H__