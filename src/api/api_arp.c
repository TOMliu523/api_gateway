/*****************************************************************************
 * filename: api_arp.c
 * function:
 * description:
 ****************************************************************************/

#include <jansson.h>

#include "log.h"
#include "api.h"

API_POST(arp, /v1/arp)
{
    LOG_INFO("POST: /v1/arp");

    return 0;
}

API_PATCH(arp, /v1/arp)
{
    LOG_INFO("PATHC: /v1/arp");
    return 0;
}

API_GET(arp, /v1/arp)
{
    return 0;
}

API_DELETE(arp, /v1/arp)
{
    return 0;
}