#ifndef UTPDRV_UTP_HANDLER_H
#define UTPDRV_UTP_HANDLER_H

// -------------------------------------------------------------------
//
// utp_handler.h: base class for uTP port handlers
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

#include <map>
#include "socket_handler.h"
#include "utils.h"
#include "drv_types.h"
#include "write_queue.h"


namespace UtpDrv {

class UtpHandler : public SocketHandler
{
public:
    ~UtpHandler();

    void outputv(ErlIOVec& ev);

    void stop();

    void input_ready();

    static void send_to(void* data, const byte* p, size_t len,
                        const sockaddr* to, socklen_t slen);
    static void utp_read(void* data, const byte* bytes, size_t count);
    static void utp_write(void* data, byte* bytes, size_t count);
    static size_t utp_get_rb_size(void* data);
    static void utp_state_change(void* data, int state);
    static void utp_error(void* data, int errcode);
    static void utp_overhead(void* data, bool send, size_t count, int type);
    static void utp_incoming(void* data, UTPSocket* utp);

protected:
    UtpHandler(int sock, const SockOpts& so);

    void set_utp_callbacks();
    void set_empty_utp_callbacks();

    ErlDrvSSizeT
    control(unsigned command, const char* buf, ErlDrvSizeT len,
            char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    peername(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    close(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    setopts(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    recv(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    ErlDrvSSizeT cancel_send();
    ErlDrvSSizeT cancel_recv();

    ErlDrvSSizeT new_controlling_process();

    void close_utp();

    void reset_waiting_recv();

    virtual void do_send_to(const byte* p, size_t len, const sockaddr* to,
                            socklen_t slen);
    virtual void do_read(const byte* bytes, size_t count);
    virtual void do_write(byte* bytes, size_t count);
    virtual size_t do_get_rb_size();
    virtual void do_state_change(int state);
    virtual void do_error(int errcode);
    virtual void do_overhead(bool send, size_t count, int type);
    virtual void do_incoming(UTPSocket* utp) = 0;

    enum UtpPortStatus {
        not_connected,
        listening,
        connect_pending,
        connected,
        connect_failed,
        closing,
        destroying,
        stopped
    };

    WriteQueue write_queue;
    Binary caller_ref;
    ErlDrvTermData caller;
    UTPSocket* utp;
    ErlDrvSizeT recv_len;
    UtpPortStatus status;
    int state, error_code;
    bool writable, sender_waiting, receiver_waiting, eof_seen;
};

}



// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
