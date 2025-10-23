/*****************************************************************************
 * filename: rserver.h
 * function:
 * description:
 ****************************************************************************/

#pragma once

#include "conf.h"
#include "rserver.h"

extern int rs_conf_search(uint32_t *, void *, int, const union inet_addr *, uint16_t);

extern void rs_conf_rs_free(void *);
extern struct rserver_v4 *rs_conf_v4_alloc(int);
extern struct rserver_v6 *rs_conf_v6_alloc(int);

extern const struct rserver *rs_conf_get_by_id(const void *, uint32_t);
extern int rs_conf_get_all(const void *, struct rserver *rss[], int);

extern void rs_conf_refcnt_inc(void *arg, uint32_t);

extern int rs_conf_get(void *, int *, struct rserver *[], int);
extern void rs_conf_batch_refcnt_dec(void *, uint32_t [], int);

extern void rs_conf_table_destroy(void *arg);
extern int rs_conf_add(void **, void *, struct rserver **, int, int);

extern void rs_conf_part_free(struct rserver *);
extern struct rserver_v4 *rs_conf_v4_part_clone(const struct rserver_v4 *, int);
extern struct rserver_v6 *rs_conf_v6_part_clone(const struct rserver_v6 *, int);
extern int rs_conf_find(const void *, struct rserver **, int, const union inet_addr *, uint16_t);

extern int rs_conf_table_update(void **, void *, struct rserver **, int, int);