// -------------------------------------------------------------------
//
// utils.cc: utilities for uTP driver
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
#include "utils.h"
#include "coder.h"
#include "globals.h"
#include "utp_port.h"


using namespace UtpDrv;

const UtpDrv::NoMemError UtpDrv::enomem_error;

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
    addrinfo *ai;
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
UtpDrv::SockAddr::encode(char** rbuf) const
{
    char addrstr[INET6_ADDRSTRLEN];
    unsigned short port;
    try {
        to_addrport(addrstr, sizeof addrstr, port);
    } catch (const BadSockAddr&) {
        return encode_error(rbuf, errno);
    }
    EiEncoder encoder;
    encoder.tuple_header(2).atom("ok");
    encoder.tuple_header(2).string(addrstr).ulong(port);
    ErlDrvSSizeT size;
    *rbuf = reinterpret_cast<char*>(encoder.copy_to_binary(size));
    return size;
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

//--------------------------------------------------------------------

ErlDrvSSizeT
UtpDrv::encode_atom(char** rbuf, const char* atom)
{
    EiEncoder encoder;
    try {
        encoder.atom(atom);
        ErlDrvSSizeT size;
        *rbuf = reinterpret_cast<char*>(encoder.copy_to_binary(size));
        return size;
    } catch (std::exception&) {
        memcpy(*rbuf, enomem_error.buffer(), enomem_error.size());
        return enomem_error.size();
    }
}

ErlDrvSSizeT
UtpDrv::encode_error(char** rbuf, const char* error)
{
    EiEncoder encoder;
    try {
        encoder.tuple_header(2).atom("error").atom(error);
        ErlDrvSSizeT size;
        *rbuf = reinterpret_cast<char*>(encoder.copy_to_binary(size));
        return size;
    } catch (std::exception&) {
        memcpy(*rbuf, enomem_error.buffer(), enomem_error.size());
        return enomem_error.size();
    }
}

ErlDrvSSizeT
UtpDrv::encode_error(char** rbuf, int error)
{
    return encode_error(rbuf, erl_errno_id(error));
}

//--------------------------------------------------------------------

int
UtpDrv::open_udp_socket(int& udp_sock, unsigned short port)
{
    SockAddr addr("0.0.0.0", port);
    return open_udp_socket(udp_sock, addr);
}

int
UtpDrv::open_udp_socket(int& udp_sock, const SockAddr& addr)
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
    if (bind(udp_sock, addr, addr.slen) < 0) {
        ::close(udp_sock);
        return errno;
    }
    return 0;
}

ErlDrvPort
UtpDrv::create_port(ErlDrvTermData owner, UtpPort* p)
{
    ErlDrvData port_drv_data = reinterpret_cast<ErlDrvData>(p);
    ErlDrvPort new_port = driver_create_port(main_port->drv_port(), owner,
                                             drv_name, port_drv_data);
    return new_port;
}

//--------------------------------------------------------------------

UtpDrv::NoMemError::NoMemError() : bin(0)
{
    encode_error(reinterpret_cast<char**>(&bin), ENOMEM);
}

UtpDrv::NoMemError::~NoMemError()
{
    if (bin != 0) {
        driver_free_binary(bin);
    }
}

const void*
UtpDrv::NoMemError::buffer() const
{
    return bin->orig_bytes;
}

size_t
UtpDrv::NoMemError::size() const
{
    return bin->orig_size;
}
