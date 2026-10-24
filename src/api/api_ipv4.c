/*****************************************************************************
 * filename: api_ipv4.c
 * function:
 * description:
 ****************************************************************************/

#include <jansson.h>

#include "log.h"
#include "api.h"

API_POST(ipv4, /v1/ipv4)
{
    return 0;
}

API_PATCH(ipv4, /v1/ipv4)
{
    return 0;
}

API_DELETE(ipv4, /v1/ipv4)
{
    return 0;
}

API_GET(ipv4, /v1/ipv4)
{
    LOG_INFO("ipv4");
    return 0;
}