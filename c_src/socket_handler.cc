// -------------------------------------------------------------------
//
// socket_handler.h: abstract base class for handlers owning a socket
//
// Copyright (c) 2012-2013 Basho Technologies, Inc. All Rights Reserved.
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
#include <stdexcept>
#include <string>
#include "socket_handler.h"
#include "globals.h"
#include "utils.h"
#include "locker.h"


using namespace UtpDrv;

UtpDrv::SocketHandler::~SocketHandler()
{
}

UtpDrv::SocketHandler::SocketHandler() : pdl(0), udp_sock(INVALID_SOCKET)
{
}

UtpDrv::SocketHandler::SocketHandler(int fd, const SockOpts& so) :
    sockopts(so), pdl(0), udp_sock(fd)
{
}

void
UtpDrv::SocketHandler::set_port(ErlDrvPort p)
{
    UTPDRV_TRACER << "SocketHandler::set_port " << this << UTPDRV_TRACE_ENDL;
    Handler::set_port(p);
    pdl = driver_pdl_create(port);
    if (pdl == 0) {
        driver_failure_atom(port, const_cast<char*>("port_data_lock_failed"));
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
UtpDrv::SocketHandler::control(unsigned command, const char* buf, ErlDrvSizeT len,
                               char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "SocketHandler::control " << this << UTPDRV_TRACE_ENDL;
    switch (command) {
    case UTP_CLOSE:
        return close(buf, len, rbuf, rlen);
    case UTP_SOCKNAME:
        return sockname(buf, len, rbuf, rlen);
    case UTP_PEERNAME:
        return peername(buf, len, rbuf, rlen);
    case UTP_SETOPTS:
        return setopts(buf, len, rbuf, rlen);
    case UTP_GETOPTS:
        return getopts(buf, len, rbuf, rlen);
    }
    return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_GENERAL);
}

ErlDrvSSizeT
UtpDrv::SocketHandler::sockname(const char* buf, ErlDrvSizeT len,
                                char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "SocketHandler::sockname " << this << UTPDRV_TRACE_ENDL;
    SockAddr addr;
    if (getsockname(udp_sock, addr, &addr.slen) < 0) {
        return encode_error(rbuf, rlen, errno);
    }
    return addr.encode(rbuf, rlen);
}

ErlDrvSSizeT
UtpDrv::SocketHandler::setopts(const char* buf, ErlDrvSizeT len,
                               char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "SocketHandler::setopts " << this << UTPDRV_TRACE_ENDL;
    Binary binopts;
    try {
        EiDecoder decoder(buf, len);
        int type, size;
        decoder.type(type, size);
        if (type != ERL_BINARY_EXT) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        binopts.decode(decoder, size);
    } catch (const EiError&) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    Active saved_active = sockopts.active;
    SockOpts opts(sockopts);
    try {
        opts.decode_and_merge(binopts);
    } catch (const std::invalid_argument&) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    sockopts = opts;
    if (sockopts.active != saved_active && sockopts.active != ACTIVE_FALSE) {
        Receiver rcvr;
        send_read_buffer(0, rcvr);
    }

    EiEncoder encoder;
    encoder.atom("ok");
    ErlDrvBinary** binptr = reinterpret_cast<ErlDrvBinary**>(rbuf);
    return encoder.copy_to_binary(binptr, rlen);
}

ErlDrvSSizeT
UtpDrv::SocketHandler::getopts(const char* buf, ErlDrvSizeT len,
                               char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "SocketHandler::getopts " << this << UTPDRV_TRACE_ENDL;
    Binary binopts;
    try {
        EiDecoder decoder(buf, len);
        int type, size;
        decoder.type(type, size);
        if (type != ERL_BINARY_EXT) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        binopts.decode(decoder, size);
    } catch (const EiError&) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    EiEncoder encoder;
    encoder.tuple_header(2).atom("ok");
    const char* opt = binopts.data();
    const char* end = opt + binopts.size();
    if (opt == end) {
        encoder.empty_list();
    } else {
        encoder.list_header(end-opt);
        while (opt != end) {
            switch (*opt++) {
            case UTP_ACTIVE:
                encoder.tuple_header(2).atom("active");
                if (sockopts.active == ACTIVE_FALSE) {
                    encoder.atom("false");
                } else if (sockopts.active == ACTIVE_TRUE) {
                    encoder.atom("true");
                } else {
                    encoder.atom("once");
                }
                break;
            case UTP_MODE:
                encoder.tuple_header(2).atom("mode");
                if (sockopts.delivery_mode == DATA_LIST) {
                    encoder.atom("list");
                } else {
                    encoder.atom("binary");
                }
                break;
            case UTP_SEND_TMOUT:
                encoder.tuple_header(2).atom("send_timeout");
                if (sockopts.send_tmout == -1) {
                    encoder.atom("infinity");
                } else {
                    encoder.longval(sockopts.send_tmout);
                }
                break;
            default:
            {
                EiEncoder error;
                error.tuple_header(2).atom("error").atom(erl_errno_id(EINVAL));
                ErlDrvBinary** binptr = reinterpret_cast<ErlDrvBinary**>(rbuf);
                return error.copy_to_binary(binptr, rlen);
            }
            break;
            }
        }
        encoder.empty_list();
    }
    ErlDrvBinary** binptr = reinterpret_cast<ErlDrvBinary**>(rbuf);
    return encoder.copy_to_binary(binptr, rlen);
}

void
UtpDrv::SocketHandler::send_read_buffer(ErlDrvSizeT len, const Receiver& receiver,
                                        const std::string* extra)
{
    ErlDrvSizeT total = 0;
    std::string str;
    {
        int vlen;
        PdlLocker pdl_lock(pdl);
        SysIOVec* vec = driver_peekq(port, &vlen);
        if (len == 0) {
            for (int i = 0; i < vlen; ++i) {
                total += vec[i].iov_len;
            }
        } else {
            total = len;
        }
        ErlDrvSizeT deq_size = total;
        if (extra != 0) {
            total += extra->size();
        }
        str.reserve(total);
        for (int i = 0; i < vlen; ++i) {
            str.append(vec[i].iov_base, vec[i].iov_len);
        }
        if (extra != 0) {
            str.append(*extra);
        }
        driver_deq(port, deq_size);
    }
    ErlDrvTermData data = reinterpret_cast<ErlDrvTermData>(str.data());
    if (receiver.send_to_connected) {
        ErlDrvTermData term[] = {
            ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("utp")),
            ERL_DRV_PORT, driver_mk_port(port),
            ERL_DRV_BUF2BINARY, data, total,
            ERL_DRV_TUPLE, 3,
        };
        if (sockopts.delivery_mode == DATA_LIST) {
            term[4] = ERL_DRV_STRING;
        }
        MutexLocker lock(drv_mutex);
        driver_output_term(port, term, sizeof term/sizeof *term);
    } else {
        ErlDrvTermData term[] = {
            ERL_DRV_EXT2TERM, receiver.caller_ref, receiver.caller_ref.size(),
            ERL_DRV_BUF2BINARY, data, total,
            ERL_DRV_TUPLE, 2,
        };
        if (sockopts.delivery_mode == DATA_LIST) {
            term[3] = ERL_DRV_STRING;
        }
        driver_send_term(port, receiver.caller, term, sizeof term/sizeof *term);
    }
    if (sockopts.active == ACTIVE_ONCE) {
        sockopts.active = ACTIVE_FALSE;
    }
}

UtpDrv::SocketHandler::SockOpts::SockOpts() :
    send_tmout(-1), active(ACTIVE_FALSE), fd(-1), port(0),
    delivery_mode(DATA_LIST), inet6(false), addr_set(false)
{
}

void
UtpDrv::SocketHandler::SockOpts::decode(const Binary& bin, OptsList* opts_list)
{
    const char* data = bin.data();
    const char* end = data + bin.size();
    while (data < end) {
        switch (*data++) {
        case UTP_IP:
            strcpy(addrstr, data);
            data += strlen(data) + 1;
            addr_set = true;
            if (opts_list != 0) {
                opts_list->push_back(UTP_IP);
            }
            break;
        case UTP_FD:
            fd = ntohl(*reinterpret_cast<const uint32_t*>(data));
            data += 4;
            if (opts_list != 0) {
                opts_list->push_back(UTP_FD);
            }
            break;
        case UTP_PORT:
            port = ntohs(*reinterpret_cast<const uint16_t*>(data));
            data += 2;
            if (opts_list != 0) {
                opts_list->push_back(UTP_PORT);
            }
            break;
        case UTP_LIST:
            delivery_mode = DATA_LIST;
            if (opts_list != 0) {
                opts_list->push_back(UTP_LIST);
            }
            break;
        case UTP_BINARY:
            delivery_mode = DATA_BINARY;
            if (opts_list != 0) {
                opts_list->push_back(UTP_BINARY);
            }
            break;
        case UTP_INET:
            inet6 = false;
            if (opts_list != 0) {
                opts_list->push_back(UTP_INET);
            }
            break;
        case UTP_INET6:
            inet6 = true;
            if (opts_list != 0) {
                opts_list->push_back(UTP_INET6);
            }
            break;
        case UTP_SEND_TMOUT:
            send_tmout = ntohl(*reinterpret_cast<const int32_t*>(data));
            data += 4;
            if (opts_list != 0) {
                opts_list->push_back(UTP_SEND_TMOUT);
            }
            break;
        case UTP_ACTIVE:
            active = static_cast<Active>(*data++);
            if (opts_list != 0) {
                opts_list->push_back(UTP_ACTIVE);
            }
            break;
        }
    }
    if (addr_set) {
        addr.from_addrport(addrstr, port);
    }
}

void
UtpDrv::SocketHandler::SockOpts::decode_and_merge(const Binary& bin)
{
    SockOpts so;
    OptsList opts;
    so.decode(bin, &opts);
    OptsList::iterator it = opts.begin();
    while (it != opts.end()) {
        switch (*it++) {
        case UTP_IP:
            throw std::invalid_argument("ip");
            break;
        case UTP_FD:
            throw std::invalid_argument("fd");
            break;
        case UTP_PORT:
            throw std::invalid_argument("port");
            break;
        case UTP_LIST:
        case UTP_BINARY:
        case UTP_MODE:
            delivery_mode = so.delivery_mode;
            break;
        case UTP_INET:
            throw std::invalid_argument("inet");
            break;
        case UTP_INET6:
            throw std::invalid_argument("inet6");
            break;
        case UTP_SEND_TMOUT:
            send_tmout = so.send_tmout;
            break;
        case UTP_ACTIVE:
            active = so.active;
            break;
        }
    }
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
    encoder.tuple_header(2).string(addrstr).ulongval(port);
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
