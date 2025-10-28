/*****************************************************************************
 * filename: thread_conf.h
 * function:
 * description:
 *****************************************************************************/

#pragma once

#include "ip4.h"
#include "ip6.h"

extern struct thread_config *tc_init(void *, int, int, int);
extern void tc_fini(struct thread_config *);