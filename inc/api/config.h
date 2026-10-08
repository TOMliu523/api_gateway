/*****************************************************************************
 * filename: config.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __CONFIG_H__
#define __CONFIG_H__

#include "top.h"

extern int config_boot_load(struct boot_config *config);
extern int config_subscript(void *param);
extern int config_post(void *, char *, size_t);
extern int config_put(void *, char *, size_t);
extern int config_patch(void *, char *, size_t);
extern int config_delete(void *, char *, size_t);
extern int config_get(void *, char *, size_t);
extern void config_param_clean(void *);

#endif // __CONFIG_H__