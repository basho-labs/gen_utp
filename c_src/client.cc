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
#include "drv_types.h"


using namespace UtpDrv;

UtpDrv::Client::Client(int sock, const Binary& ref,
                       DataDelivery del, long send_timeout) :
    UtpHandler(sock, del, send_timeout)
{
    UTPDRV_TRACE("Client::Client\r\n");
    caller_ref = ref;
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
        return connect_validate(buf, len, rbuf, rlen);
    case UTP_SOCKNAME:
        return UtpHandler::sockname(buf, len, rbuf, rlen);
    case UTP_PEERNAME:
        return peername(buf, len, rbuf, rlen);
    case UTP_CLOSE:
        return close(buf, len, rbuf, rlen);
    case UTP_SETOPTS:
        return setopts(buf, len, rbuf, rlen);
    case UTP_CANCEL_SEND:
        return cancel_send();
    default:
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_GENERAL);
    }
}

void
UtpDrv::Client::connect_to(const SockAddr& addr)
{
    UTPDRV_TRACE("Client::connect_to\r\n");
    status = connect_pending;
    MutexLocker lock(utp_mutex);
    utp = UTP_Create(&Client::send_to, this, addr, addr.slen);
    set_utp_callbacks(utp);
    UTP_Connect(utp);
}

void
UtpDrv::Client::do_incoming(UTPSocket* utp)
{
    UTPDRV_TRACE("Client::do_incoming\r\n");
}

ErlDrvSSizeT
UtpDrv::Client::connect_validate(const char* buf, ErlDrvSizeT len,
                                 char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACE("Client::connect_validate\r\n");
    Binary ref;
    try {
        int type, size;
        EiDecoder decoder(buf, len);
        decoder.type(type, size);
        if (type != ERL_BINARY_EXT) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        ref.decode(decoder, size);
    } catch (const EiError&) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    EiEncoder encoder;
    switch (status) {
    case connect_pending:
        encoder.atom("wait");
        caller_ref.swap(ref);
        break;

    case connect_failed:
        encoder.tuple_header(2).atom("error").atom(erl_errno_id(error_code));
        status = not_connected;
        break;

    case connected:
        encoder.atom("ok");
        break;

    default:
        encoder.tuple_header(2).atom("error");
        {
            char err[128];
            sprintf(err, "utpdrv illegal connect state: %d", status);
            encoder.string(err);
        }
        break;
    }
    ErlDrvBinary** binptr = reinterpret_cast<ErlDrvBinary**>(rbuf);
    return encoder.copy_to_binary(binptr, rlen);
}
