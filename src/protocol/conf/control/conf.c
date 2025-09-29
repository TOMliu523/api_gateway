/*****************************************************************************
 * filename: conf.c
 * function:
 * description:
 ****************************************************************************/

#include <arpa/inet.h>

#include "conf.h"
#include "macro.h"

static char s_ip_str[CACHE_LINE] = {0};

const char *conf_ip_to_str(uint32_t ip)
{
    inet_ntop(AF_INET, (const void *)&ip, s_ip_str, sizeof(s_ip_str));
    return s_ip_str;
}

const char *conf_ip6_to_str(struct dpdk_ip6_addr *ip6)
{
    inet_ntop(AF_INET6, ip6, s_ip_str, sizeof(s_ip_str));
    return s_ip_str;
}