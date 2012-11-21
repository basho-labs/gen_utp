#ifndef UTPDRV_UTP_PORT_H
#define UTPDRV_UTP_PORT_H

// -------------------------------------------------------------------
//
// utpdrv_port.h: Erlang driver port for uTP
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

#include "utpdrv_dispatcher.h"
#include "ei.h"
#include "libutp/utp.h"

namespace UtpDrv {

class Port : public Dispatcher
{
public:
    Port(Dispatcher& disp, int sock);
    ~Port();

    void connect_to(const SockAddr& addr, socklen_t slen);
    void readable();
    void set_port(ErlDrvPort port);
    void set_port(ErlDrvPort port, ErlDrvTermData owner);

    ErlDrvSSizeT
    connect_validate(const char* buf, ErlDrvSizeT len,
                     char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    close(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    send(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    ErlDrvSSizeT
    sockname(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    peername(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    void
    process_exit(ErlDrvMonitor* monitor);

    void stop();

protected:
    void set_callbacks();
    int enotconn(char** rbuf);

    enum ConnectInfo {
        not_connected,
        connect_pending,
        connected,
        connect_failed,
        closing
    };

    Dispatcher& topdisp;
    UTPSocket* utp;
    ErlDrvMonitor owner_mon;
    ConnectInfo status;
    int udp_sock, state, error_code;
    bool writable;

private:
    virtual void do_send_to(const byte* p, size_t len, const sockaddr* to,
                            socklen_t slen);
    virtual void do_read(const byte* bytes, size_t count);
    virtual void do_write(byte* bytes, size_t count);
    size_t do_get_rb_size();
    virtual void do_state_change(int state);
    virtual void do_error(int errcode);
    void do_overhead(bool send, size_t count, int type);
    virtual void do_incoming(UTPSocket* utp);

    static void send_to(void* data, const byte* p, size_t len,
                        const sockaddr* to, socklen_t slen)
    {
        (static_cast<Port*>(data))->do_send_to(p, len, to, slen);
    }
    static void utp_read(void* data, const byte* bytes, size_t count)
    {
        (static_cast<Port*>(data))->do_read(bytes, count);
    }
    static void utp_write(void* data, byte* bytes, size_t count)
    {
        (static_cast<Port*>(data))->do_write(bytes, count);
    }
    static size_t utp_get_rb_size(void* data)
    {
        return (static_cast<Port*>(data))->do_get_rb_size();
    }
    static void utp_state_change(void* data, int state)
    {
        (static_cast<Port*>(data))->do_state_change(state);
    }
    static void utp_error(void* data, int errcode)
    {
        (static_cast<Port*>(data))->do_error(errcode);
    }
    static void utp_overhead(void* data, bool send, size_t count, int type)
    {
        (static_cast<Port*>(data))->do_overhead(send, count, type);
    }
    static void utp_incoming(void* data, UTPSocket* utp)
    {
        (static_cast<Port*>(data))->do_incoming(utp);
    }

    int
    encode_addrport(ei_x_buff& xbuf, const SockAddr&addr, socklen_t slen,
                    char** rbuf, ErlDrvSizeT rlen);

    ErlDrvBinary* connect_ref;
};

}


// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
