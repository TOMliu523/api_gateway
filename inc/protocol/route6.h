
/*****************************************************************************
 * filename: route6.h
 * function:
 * description: High-Performance Longest Prefix Match Library
 ****************************************************************************/

#pragma once

extern void *route6_thread_create(int);
extern void route6_thread_destroy(void *);
extern void route6_thread_config_refresh(void *arg);