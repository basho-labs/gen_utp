// -------------------------------------------------------------------
//
// socket_handler.h: abstract base class for handlers owning a socket
//
// Copyright (c) 2012 Basho Technologies, Inc. All Rights Reserved.
//
// This file is provided to you under the Apache License,
// Version 2.0 (the "License"); you may not use this file
// except in compliance with the License.  You may obtain
// a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// -------------------------------------------------------------------

#include <unistd.h>
#include <fcntl.h>
#include "socket_handler.h"
#include "globals.h"
#include "utils.h"


using namespace UtpDrv;

UtpDrv::SocketHandler::~SocketHandler()
{
}

UtpDrv::SocketHandler::SocketHandler() : udp_sock(INVALID_SOCKET)
{
}

UtpDrv::SocketHandler::SocketHandler(int fd) : udp_sock(fd)
{
}

void
UtpDrv::SocketHandler::decode_sock_opts(const Binary& bin, SockOpts& opts)
{
    const char* data = bin.data();
    const char* end = data + bin.size();
    while (data < end) {
        switch (*data++) {
        case UTP_IP:
            strcpy(opts.addrstr, data);
            data += strlen(data) + 1;
            opts.addr_set = true;
            break;
        case UTP_FD:
            opts.fd = ntohl(*reinterpret_cast<const uint32_t*>(data));
            data += 4;
            break;
        case UTP_PORT:
            opts.port = ntohs(*reinterpret_cast<const uint16_t*>(data));
            data += 2;
            break;
        case UTP_LIST:
            opts.delivery = DATA_LIST;
            break;
        case UTP_BINARY:
            opts.delivery = DATA_BINARY;
            break;
        case UTP_INET:
            opts.inet6 = false;
            break;
        case UTP_INET6:
            opts.inet6 = true;
            break;
        case UTP_SEND_TMOUT:
            opts.send_tmout = ntohl(*reinterpret_cast<const uint32_t*>(data));
            data += 4;
            break;
        }
    }
    if (opts.addr_set) {
        opts.addr.from_addrport(opts.addrstr, opts.port);
    }
}


int
UtpDrv::SocketHandler::open_udp_socket(int& udp_sock, unsigned short port,
                                       bool reuseaddr)
{
    SockAddr addr(INADDR_ANY, port);
    return open_udp_socket(udp_sock, addr, reuseaddr);
}

int
UtpDrv::SocketHandler::open_udp_socket(int& udp_sock, const SockAddr& addr,
                                       bool reuseaddr)
{
    int family = addr.family();
    if ((udp_sock = socket(family, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        return errno;
    }
    int flags = fcntl(udp_sock, F_GETFL);
    if (fcntl(udp_sock, F_SETFL, flags|O_NONBLOCK) < 0) {
        ::close(udp_sock);
        return errno;
    }
    if (reuseaddr) {
        int on = 1;
#if defined(SO_REUSEPORT)
        int optval = SO_REUSEPORT;
#else
        int optval = SO_REUSEADDR;
#endif
        if (setsockopt(udp_sock, SOL_SOCKET, optval, &on, sizeof on) < 0) {
            ::close(udp_sock);
            return errno;
        }
    }
    if (bind(udp_sock, addr, addr.slen) < 0) {
        ::close(udp_sock);
        return errno;
    }
    return 0;
}

ErlDrvSSizeT
UtpDrv::SocketHandler::sockname(const char* buf, ErlDrvSizeT len,
                                char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACE("SocketHandler::sockname\r\n");
    SockAddr addr;
    if (getsockname(udp_sock, addr, &addr.slen) < 0) {
        return encode_error(rbuf, rlen, errno);
    }
    return addr.encode(rbuf, rlen);
}

UtpDrv::SocketHandler::SockOpts::SockOpts() :
    send_tmout(-1), fd(-1), port(0), delivery(DATA_BINARY),
    inet6(false), addr_set(false)
{
}

UtpDrv::SockAddr::SockAddr() : slen(sizeof addr)
{
    memset(&addr, 0, slen);
}

UtpDrv::SockAddr::SockAddr(const sockaddr& sa, socklen_t sl) : slen(sl)
{
    memcpy(&addr, &sa, slen);
}

UtpDrv::SockAddr::SockAddr(const char* addrstr, unsigned short port)
{
    from_addrport(addrstr, port);
}

UtpDrv::SockAddr::SockAddr(in_addr_t inaddr, unsigned short port)
{
    from_addrport(inaddr, port);
}

UtpDrv::SockAddr::SockAddr(const in6_addr& inaddr6, unsigned short port)
{
    from_addrport(inaddr6, port);
}

void
UtpDrv::SockAddr::from_addrport(const char* addrstr, unsigned short port)
{
    memset(&addr, 0, sizeof addr);
    sockaddr* sa = reinterpret_cast<sockaddr*>(&addr);
    addrinfo hints;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = PF_UNSPEC;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_NUMERICHOST;
    addrinfo* ai;
    if (getaddrinfo(addrstr, 0, &hints, &ai) != 0) {
        throw BadSockAddr();
    }
    memcpy(sa, ai->ai_addr, ai->ai_addrlen);
    slen = ai->ai_addrlen;
    freeaddrinfo(ai);
    if (sa->sa_family == AF_INET) {
        sockaddr_in* sa_in = reinterpret_cast<sockaddr_in*>(sa);
        sa_in->sin_port = htons(port);
    } else if (sa->sa_family == AF_INET6) {
        sockaddr_in6* sa_in6 = reinterpret_cast<sockaddr_in6*>(sa);
        sa_in6->sin6_port = htons(port);
    } else {
        throw BadSockAddr();
    }
}

void
UtpDrv::SockAddr::from_addrport(in_addr_t inaddr, unsigned short port)
{
    memset(&addr, 0, sizeof addr);
    sockaddr_in* sa_in = reinterpret_cast<sockaddr_in*>(&addr);
    sa_in->sin_family = AF_INET;
    sa_in->sin_addr.s_addr = inaddr;
    sa_in->sin_port = htons(port);
    slen = sizeof(sockaddr_in);
}

void
UtpDrv::SockAddr::from_addrport(const in6_addr& inaddr6, unsigned short port)
{
    memset(&addr, 0, sizeof addr);
    sockaddr_in6* sa_in6 = reinterpret_cast<sockaddr_in6*>(&addr);
    sa_in6->sin6_family = AF_INET6;
    sa_in6->sin6_addr = inaddr6;
    sa_in6->sin6_port = htons(port);
    slen = sizeof(sockaddr_in);
}

void
UtpDrv::SockAddr::to_addrport(char* addrstr, size_t addrlen,
                              unsigned short& port) const
{
    const sockaddr* sa = reinterpret_cast<const sockaddr*>(&addr);
    if (getnameinfo(sa, slen, addrstr, addrlen, 0, 0, NI_NUMERICHOST) != 0) {
        throw BadSockAddr();
    }
    if (sa->sa_family == AF_INET) {
        const sockaddr_in* sa_in = reinterpret_cast<const sockaddr_in*>(sa);
        port = ntohs(sa_in->sin_port);
    } else if (sa->sa_family == AF_INET6) {
        const sockaddr_in6* sa_in6 = reinterpret_cast<const sockaddr_in6*>(sa);
        port = ntohs(sa_in6->sin6_port);
    } else {
        throw BadSockAddr();
    }
}

int
UtpDrv::SockAddr::family() const
{
    const sockaddr* sa = reinterpret_cast<const sockaddr*>(&addr);
    return sa->sa_family;
}

ErlDrvSSizeT
UtpDrv::SockAddr::encode(char** rbuf, ErlDrvSizeT rlen) const
{
    char addrstr[INET6_ADDRSTRLEN];
    unsigned short port;
    try {
        to_addrport(addrstr, sizeof addrstr, port);
    } catch (const BadSockAddr&) {
        return encode_error(rbuf, rlen, errno);
    }
    EiEncoder encoder;
    encoder.tuple_header(2).atom("ok");
    encoder.tuple_header(2).string(addrstr).ulong(port);
    ErlDrvBinary** binptr = reinterpret_cast<ErlDrvBinary**>(rbuf);
    return encoder.copy_to_binary(binptr, rlen);
}

bool
UtpDrv::SockAddr::operator<(const SockAddr& sa) const
{
    if (addr.ss_family == sa.addr.ss_family) {
        return memcmp(&addr, &sa, slen) < 0;
    } else {
        return addr.ss_family < sa.addr.ss_family;
    }
}

UtpDrv::SockAddr::operator sockaddr*()
{
    return reinterpret_cast<sockaddr*>(&addr);
}

UtpDrv::SockAddr::operator const sockaddr*() const
{
    return reinterpret_cast<const sockaddr*>(&addr);
}
