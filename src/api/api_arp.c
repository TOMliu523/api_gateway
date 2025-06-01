/************************************************
 * filename: api_arp.c
 * function:
 * description:
 ***********************************************/

#include "api_inner.h"

API_POST(/v1/network/arp, arp)
{
    return api_success(NULL);
}

API_PUT(/v1/network/arp, arp)
{
    return NULL;
}

API_DELETE(/v1/network/arp, arp)
{
    return NULL;
}

API_GET(/v1/network/arp, arp)
{
    return api_success(NULL);
}