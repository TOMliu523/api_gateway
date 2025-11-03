/*****************************************************************************
 * filename: protocol.h
 * function:
 * description:
 ****************************************************************************/

#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

enum PROTO_TYPE {
    PROTO_TCP,
    PROTO_UDP,
    PROTO_HTTP,
    PROTO_HTTPS,
    PROTO_HTTP2,
    PROTO_HTTP3,
    PROTO_MAX,
};

#endif // __PROTOCOL_H__