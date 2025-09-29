/*****************************************************************************
 * filename: rserver.h
 * function:
 * description:
 ****************************************************************************/

#pragma once

#include "rserver.h"

extern void rs_conf_rs_free(struct rserver *);
extern struct rserver_v4 *rs_conf_v4_alloc(int);
extern struct rserver_v6 *rs_conf_v6_alloc(int);

extern int rs_conf_add(void *, struct rserver **rs, int count);
extern void rs_conf_add_del(void *, struct rserver **rs, int count);