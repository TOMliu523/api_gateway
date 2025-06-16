/************************************************
 * filename: dataplane.h
 * function:
 * description:
 ***********************************************/

#ifndef __DATAPLANE_H__
#define __DATAPLANE_H__

#include <stdint.h>

#include "macro.h"

struct dataplane {
    uint16_t cpu_id;
    uint8_t socket_id;
    uint8_t index;
} ALIGNED(CACHE_LINE);

int dp_startup(void *arg);

#endif // __DATAPLANE_H__