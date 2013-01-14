#ifndef UTPDRV_SOCKET_HANDLER_H
#define UTPDRV_SOCKET_HANDLER_H

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

#include <vector>
#include <string>
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
    bool operator==(const SockAddr& sa) const { return !(*this < sa || sa < *this); }
    operator sockaddr*();
    operator const sockaddr*() const;
};

class SocketHandler : public Handler
{
public:
    ~SocketHandler();

    // the following enums must match option values in gen_utp_opts.hrl
    enum Opts {
        UTP_IP_OPT = 1,
        UTP_PORT_OPT,
        UTP_LIST_OPT,
        UTP_BINARY_OPT,
        UTP_MODE_OPT,
        UTP_INET_OPT,
        UTP_INET6_OPT,
        UTP_SEND_TMOUT_OPT,
        UTP_SEND_TMOUT_INFINITE_OPT,
        UTP_ACTIVE_OPT
    };
    typedef std::vector<Opts> OptsList;

    enum Active {
        ACTIVE_FALSE,
        ACTIVE_ONCE,
        ACTIVE_TRUE
    };

    struct SockOpts {
        SockOpts();

        void decode(const Binary& bin, OptsList* opts_decoded = 0);
        void decode_and_merge(const Binary& bin);

        SockAddr addr;
        char addrstr[INET6_ADDRSTRLEN];
        long send_tmout;
        Active active;
        int fd;
        unsigned short port;
        DeliveryMode delivery_mode;
        bool inet6;
        bool addr_set;
    };

    virtual void set_port(ErlDrvPort p);

    static int
    open_udp_socket(int& udp_sock, unsigned short port = 0,
                    bool reuseaddr = false);
    static int
    open_udp_socket(int& udp_sock, const SockAddr& sa, bool reuseaddr = false);

    ErlDrvSSizeT
    control(unsigned command, const char* buf, ErlDrvSizeT len,
            char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    sockname(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual void input_ready() = 0;

protected:
    SocketHandler();
    SocketHandler(int fd, const SockOpts& so);

    virtual ErlDrvSSizeT
    close(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen) = 0;

    virtual ErlDrvSSizeT
    setopts(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    getopts(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    peername(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen) = 0;

    // In the send_read_buffer function, a Receiver object is used to
    // determine where to send the data. If the send_to_connected field is
    // true, send the message to the connected process; if false, send it
    // to the process identified by the caller field, including the
    // caller_ref in the message.
    struct Receiver {
        Receiver() : send_to_connected(true) {}
        Receiver(bool b, ErlDrvTermData td, const Binary& bin) :
            caller_ref(bin), caller(td), send_to_connected(b) {}
        Binary caller_ref;
        ErlDrvTermData caller;
        bool send_to_connected;
    };

    ErlDrvSizeT
    send_read_buffer(ErlDrvSizeT len, const Receiver& receiver,
                     const ustring* extra_data = 0);

    SockOpts sockopts;
    ErlDrvPDL pdl;
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
