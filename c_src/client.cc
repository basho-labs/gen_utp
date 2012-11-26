// -------------------------------------------------------------------
//
// client.cc: uTP client port
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

#include "client.h"
#include "globals.h"
#include "locker.h"


using namespace UtpDrv;

UtpDrv::Client::Client(int sock) : UtpPort(sock)
{
    UTPDRV_TRACE("Client::Client\r\n");
}

UtpDrv::Client::~Client()
{
    UTPDRV_TRACE("Client::~Client\r\n");
}

ErlDrvSSizeT
UtpDrv::Client::control(unsigned command, const char* buf, ErlDrvSizeT len,
                        char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACE("Client::control\r\n");
    switch (command) {
    case UTP_CONNECT_VALIDATE:
        return connect_validate(buf, len, rbuf);
    case UTP_SOCKNAME:
        return UtpPort::sockname(buf, len, rbuf);
    case UTP_PEERNAME:
        return peername(buf, len, rbuf);
    case UTP_SEND:
        return send(buf, len, rbuf);
    case UTP_CLOSE:
        return close(buf, len, rbuf);
    default:
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_GENERAL);
    }
}

void
UtpDrv::Client::stop()
{
    UTPDRV_TRACE("Client::stop\r\n");
    if (main_port) {
        main_port->deselect(udp_sock);
    }
}

void
UtpDrv::Client::connect_to(const SockAddr& addr)
{
    UTPDRV_TRACE("Client::connect_to\r\n");
    status = connect_pending;
    if (caller_ref != 0) {
        driver_free_binary(caller_ref);
        caller_ref = 0;
    }
    MutexLocker lock(utp_mutex);
    utp = UTP_Create(&Client::send_to, this, addr, addr.slen);
    set_utp_callbacks(utp);
    UTP_Connect(utp);
}

void
UtpDrv::Client::do_send_to(const byte* p, size_t len,
                           const sockaddr* to, socklen_t slen)
{
    UTPDRV_TRACE("Client::do_send_to\r\n");
    UtpPort::do_send_to(p, len, to, slen);
}

void
UtpDrv::Client::do_read(const byte* bytes, size_t count)
{
    UTPDRV_TRACE("Client::do_read\r\n");
    UtpPort::do_read(bytes, count);
}

void
UtpDrv::Client::do_write(byte* bytes, size_t count)
{
    UTPDRV_TRACE("Client::do_write\r\n");
    UtpPort::do_write(bytes, count);
}

size_t
UtpDrv::Client::do_get_rb_size()
{
    UTPDRV_TRACE("Client::do_get_rb_size\r\n");
    return UtpPort::do_get_rb_size();
}

void
UtpDrv::Client::do_state_change(int s)
{
    UTPDRV_TRACE("Client::do_state_change\r\n");
    UtpPort::do_state_change(s);
}

void
UtpDrv::Client::do_error(int errcode)
{
    UTPDRV_TRACE("Client::do_error\r\n");
    UtpPort::do_error(errcode);
}

void
UtpDrv::Client::do_overhead(bool send, size_t count, int type)
{
    UtpPort::do_overhead(send, count, type);
}

void
UtpDrv::Client::do_incoming(UTPSocket* utp)
{
    UTPDRV_TRACE("Client::do_incoming\r\n");
}

ErlDrvSSizeT
UtpDrv::Client::connect_validate(const char* buf, ErlDrvSizeT len, char** rbuf)
{
    UTPDRV_TRACE("Client::connect_validate\r\n");
    ErlDrvBinary* ref = 0;
    long bin_size;
    try {
        int type, size;
        EiDecoder decoder(buf, len);
        decoder.type(type, size);
        if (type != ERL_BINARY_EXT) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        ref = driver_alloc_binary(size);
        decoder.binary(ref->orig_bytes, bin_size);
    } catch (const EiError&) {
        if (ref != 0) {
            driver_free_binary(ref);
        }
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    EiEncoder encoder;
    switch (status) {
    case connect_pending:
        encoder.atom("wait");
        caller_ref = ref;
        break;

    case connect_failed:
        encoder.tuple_header(2).atom("error").atom(erl_errno_id(error_code));
        status = not_connected;
        driver_free_binary(ref);
        break;

    case connected:
        encoder.atom("ok");
        driver_free_binary(ref);
        break;

    default:
        encoder.tuple_header(2).atom("error");
        {
            char err[64];
            sprintf(err, "utpdrv illegal connect state: %d", status);
            encoder.string(err);
        }
        driver_free_binary(ref);
        break;
    }
    ErlDrvSSizeT retsize;
    *rbuf = reinterpret_cast<char*>(encoder.copy_to_binary(retsize));
    return retsize;
}
