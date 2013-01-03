#ifndef UTPDRV_SOCKET_HANDLER_H
#define UTPDRV_SOCKET_HANDLER_H

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

#include "handler.h"
#include "drv_types.h"


namespace UtpDrv {

struct BadSockAddr : public std::exception {};
struct SocketFailure : public std::exception
{
    explicit SocketFailure(int err) : error(err) {}
    int error;
};

struct SockAddr {
    sockaddr_storage addr;
    socklen_t slen;
    SockAddr();
    SockAddr(const sockaddr& sa, socklen_t sl);
    SockAddr(const char* addrstr, unsigned short port);
    SockAddr(in_addr_t inaddr, unsigned short port);
    SockAddr(const in6_addr& inaddr6, unsigned short port);
    void from_addrport(const char* addrstr, unsigned short port);
    void from_addrport(in_addr_t inaddr, unsigned short port);
    void from_addrport(const in6_addr& inaddr6, unsigned short port);
    void to_addrport(char* addrstr, size_t alen, unsigned short& port) const;
    int family() const;
    ErlDrvSSizeT encode(char** rbuf, ErlDrvSizeT rlen) const;
    bool operator<(const SockAddr& sa) const;
    operator sockaddr*();
    operator const sockaddr*() const;
};

class SocketHandler : public Handler
{
public:
    ~SocketHandler();

    enum {
        UTP_IP = 1,
        UTP_FD,
        UTP_PORT,
        UTP_LIST,
        UTP_BINARY,
        UTP_INET,
        UTP_INET6,
        UTP_SEND_TMOUT
    };

    struct SockOpts {
        SockOpts();
        SockAddr addr;
        char addrstr[INET6_ADDRSTRLEN];
        long send_tmout;
        int fd;
        unsigned short port;
        DataDelivery delivery;
        bool inet6;
        bool addr_set;
    };

    static void decode_sock_opts(const Binary& opts, SockOpts&);

    static int
    open_udp_socket(int& udp_sock, unsigned short port = 0,
                    bool reuseaddr = false);
    static int
    open_udp_socket(int& udp_sock, const SockAddr& sa, bool reuseaddr = false);

    virtual ErlDrvSSizeT
    sockname(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual void input_ready() = 0;

protected:
    SocketHandler();
    explicit SocketHandler(int fd);

    virtual ErlDrvSSizeT
    close(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen) = 0;

    int udp_sock;
};

}



// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
