/*****************************************************************************
 * filename: errcode.h
 * function:
 * description:
 *****************************************************************************/

#ifndef __ERRCODE_H__
#define __ERRCODE_H__

#define ERRCODE_EXTEND(XX) \
    XX(SUCCESS, 0, "OK") \
    XX(REDIRECT, 1, "Redirect") \
    XX(USER_PWD, 2, "User password error") \
    XX(EXPIRED, 3, "Login expired") \
    XX(AUTH, 4, "Unauthorized") \
    XX(FORBIDDEN, 5, "Access forbidden") \
    XX(INNER, 6, "Server inner error") \
    XX(RESOURCE_BUSY, 7, "Resource busy") \
    XX(OOM, 8, "OOM") \
    XX(NOT_SUPPORT, 9, "Not support") \
    \
    XX(SYSTEM, 100, "System question") \
    XX(ACCOUNT, 101, "Account or password error") \
    \
    XX(NETWORK, 200, "Network question") \
    XX(PORT_TOO_MANY, 201, "Port too many") \
    XX(PORT_NOT_EXIST, 202, "Port not exist") \
    XX(PORT_IS_DOWN, 203, "Port is down") \
    XX(MIX_IP, 204, "IPv4 and IPv6 mixing") \
    XX(IP_EXIST, 205, "IP exist") \
    XX(IP_NOT_EXIST, 206, "IP not exist") \
    XX(IP_LIMIT_EXCEEDED, 207, "Maximum supported IP address count exceeded") \
    XX(SUBNET_EXIST, 208, "Subnet already exist") \
    \
    XX(ROUTE_CONFLICT, 300, "Route conflict") \
    XX(ROUTE_NEXTHOP_UNREACHABLE, 301, "Nexthop unreachable") \
    XX(ROUTE_MULTI_DEFAULT, 302, "Multi default route") \
    XX(ROUTE_LOCAL_IP, 303, "Nexthop is local ip") \
    XX(ROUTE_NEXTHOP_INVALID, 304, "Nexthop invalid") \
    XX(SUBNET_NO_EXIST, 305, "Subnext no exist") \
    XX(ROUTE_REFERENCED, 306, "Route referenced") \
    XX(SUBNET_INVALID, 307, "Subnet invalid") \
    XX(RSERVER_TOO_MANY, 400, "Real server too many") \
    XX(RSERVER_DUPLICATE, 401, "Real server duplicate") \
    XX(RSERVER_INVALID_COUNT, 402, "Real server invalid count") \
    XX(RSERVER_NOT_FOUND, 403, "Real server not found")

enum ERRCODE {
#define ERRCODE(code, value, msg) ERRCODE_##code = value,
    ERRCODE_INVALID = -1,

    ERRCODE_EXTEND(ERRCODE)

    ERRCODE_MAX
#undef ERRCODE
};

#endif // __ERRCODE_H__