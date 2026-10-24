/*****************************************************************************
 * filename: api_ipv6.c
 * function:
 * description:
 ****************************************************************************/

#include <jansson.h>

#include "log.h"
#include "api.h"

API_POST(ipv6, /v1/ipv6)
{
    return 0;
}

API_PATCH(ipv6, /v1/ipv6)
{
    return 0;
}

API_DELETE(ipv6, /v1/ipv6)
{
    return 0;
}

API_GET(ipv6, /v1/ipv6)
{
    LOG_INFO("ipv6");
    return 0;
}