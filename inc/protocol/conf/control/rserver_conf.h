/*****************************************************************************
 * filename: rserver.h
 * function:
 * description:
 ****************************************************************************/

#pragma once

#include "conf.h"
#include "rserver.h"

extern void rs_conf_free(void *);
extern struct rserver4 *rs_conf_v4_alloc(int);
extern struct rserver6 *rs_conf_v6_alloc(int);

extern int rs_conf_table_get_count(int *, const void *);
extern int rs_conf_table_get_element(const void *, struct rserver *[], int *);
extern int rs_conf_table_create_and_append(void **, void *, struct rserver *[], int, int);
extern int rs_conf_table_create_and_delete(void **, void *, struct rserver *[], int, int);
extern void rs_conf_table_destroy(void *);

extern int rs_conf_get_by_key(struct rserver **, void *, int, const union inet_addr *, uint16_t);
extern int rs_conf_get_by_id(struct rserver **, void *, uint32_t);