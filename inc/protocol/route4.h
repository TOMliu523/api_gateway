/*****************************************************************************
 * filename: route4.h
 * function:
 * description:
 ****************************************************************************/

#pragma once

extern void *route4_thread_create(int);
extern void route4_thread_destroy(void *);
extern void route4_thread_config_refresh(void *arg);