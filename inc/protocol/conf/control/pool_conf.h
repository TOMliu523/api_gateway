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
extern int pool_conf_get_by_name(struct pool **, void *, const char *);
extern int pool_conf_get_by_id(void *, struct pool **, uint32_t);
extern struct pool *pool_conf_create(const char *, enum RS_SELECT_ALGO, int, int);

extern void pool_conf_table_destroy(void *);
extern int pool_conf_table_append(void **, void *, struct pool **, int, int);
extern int pool_conf_table_del(void **, void *, struct pool **, int, int);
extern int pool_conf_table_get_count(const void *, int *);
int pool_conf_table_get_element(const struct pool *[], const void *, int);

#endif // __POOL_CONF_H__