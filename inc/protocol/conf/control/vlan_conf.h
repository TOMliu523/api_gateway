/*****************************************************************************
 * filename: vlan_conf.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __VLAN_CONF_H__
#define __VLAN_CONF_H__

#include "vlan.h"

extern const struct vlan *vlan_conf_get_by_name(void *, const char *);

#endif // __VLAN_CONF_H__