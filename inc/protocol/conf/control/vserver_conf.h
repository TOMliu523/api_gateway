/*****************************************************************************
 * filename: vserver_conf.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __VSERVER_CONF_H__
#define __VSERVER_CONF_H__

#include "vserver.h"

extern int vs_conf_get_by_name(void *, struct vserver **, const char *);
extern void vs_conf_table_destroy(void *);
extern int vs_conf_table_create_and_append(void **, void *, struct vserver **, int, int);
extern int vs_conf_table_create_and_delete(void **, void *, struct vserver **, int, int);
extern int vs_conf_table_get_count(int *, const void *);
extern int vs_conf_table_get_element(struct vserver *[], int, void *);

#endif // __VSERVER_CONF_H__