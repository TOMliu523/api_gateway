/*****************************************************************************
 * filename: snat_pool_conf.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __SNAT_POOL_CONF_H__
#define __SNAT_POOL_CONF_H__

#include "snat_pool.h"

extern int snat_conf_table_create(void **, void *, struct snat_pool [], int);
extern void snat_conf_table_destroy(void *);
extern int snat_conf_get_by_id(void *, struct snat_pool **, uint32_t);
extern int snat_conf_get_by_name(void *, struct snat_pool **, const char *);
extern int snat_conf_table_add(void **, const void *arg, struct snat_pool **, int, int);
extern int snat_conf_get_all(const void *, struct snat_pool *[], int *);

#endif // __SNAT_POOL_CONF_H__