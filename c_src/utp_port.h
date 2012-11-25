#ifndef UTPDRV_UTP_PORT_H
#define UTPDRV_UTP_PORT_H

// -------------------------------------------------------------------
//
// utp_port.h: base class for created uTP ports
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


namespace UtpDrv {

class UtpPort : public Handler
{
public:
    ~UtpPort();

    void process_exit(ErlDrvMonitor* monitor);

    virtual bool
    set_port(ErlDrvPort p, ErlDrvTermData proc);

    virtual void
    input_ready();

protected:
    UtpPort(int sock);

    void set_utp_callbacks(UTPSocket* utp);

    virtual ErlDrvSSizeT
    sockname(const char* buf, ErlDrvSizeT len, char** rbuf);

    virtual ErlDrvSSizeT
    peername(const char* buf, ErlDrvSizeT len, char** rbuf);

    virtual ErlDrvSSizeT
    send(const char* buf, ErlDrvSizeT len, char** rbuf);

    virtual ErlDrvSSizeT
    close(const char* buf, ErlDrvSizeT len, char** rbuf);

    virtual void do_send_to(const byte* p, size_t len, const sockaddr* to,
                            socklen_t slen) = 0;
    virtual void do_read(const byte* bytes, size_t count) = 0;
    virtual void do_write(byte* bytes, size_t count) = 0;
    virtual size_t do_get_rb_size() = 0;
    virtual void do_state_change(int state) = 0;
    virtual void do_error(int errcode) = 0;
    virtual void do_overhead(bool send, size_t count, int type) = 0;
    virtual void do_incoming(UTPSocket* utp) = 0;

    static void send_to(void* data, const byte* p, size_t len,
                        const sockaddr* to, socklen_t slen);
    static void utp_read(void* data, const byte* bytes, size_t count);
    static void utp_write(void* data, byte* bytes, size_t count);
    static size_t utp_get_rb_size(void* data);
    static void utp_state_change(void* data, int state);
    static void utp_error(void* data, int errcode);
    static void utp_overhead(void* data, bool send, size_t count, int type);
    static void utp_incoming(void* data, UTPSocket* utp);

    enum PortStatus {
        not_connected,
        listening,
        connect_pending,
        connected,
        connect_failed,
        closing
    };

    void demonitor();

    ErlDrvPort port;
    ErlDrvTermData owner;
    ErlDrvMonitor mon;
    ErlDrvPDL pdl;
    ErlDrvTermData caller;
    UTPSocket* utp;
    ErlDrvBinary* caller_ref;
    PortStatus status;
    int udp_sock, state, error_code;
    bool writable, mon_valid;

private:

};

}



// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
