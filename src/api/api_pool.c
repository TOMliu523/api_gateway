/*****************************************************************************
 * filename: api_pool.c
 * function:
 * description:
 ****************************************************************************/

#include <arpa/inet.h>
#include <linux/netfilter.h>

#include "log.h"
#include "type.h"
#include "errcode.h"
#include "api_inner.h"
#include "dpdk_common.h"

#define API_POOL_MODULE_NAME "pool"
#define API_POOL_LIST_NAME "entrys"

API_POST(/v1/app/pool, pool)
{
    return api_succ(NULL);
}

API_PUT(/v1/app/pool, pool)
{
    return api_succ(NULL);
}

API_DEL(/v1/app/pool, pool)
{
    return api_succ(NULL);
}

API_GET(/v1/app/pool, pool)
{
    return api_succ(NULL);
}