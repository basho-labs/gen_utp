// -------------------------------------------------------------------
//
// server.cc: uTP server port
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

#include "server.h"
#include "listener.h"
#include "globals.h"
#include "locker.h"


using namespace UtpDrv;

UtpDrv::Server::Server(Listener& lr, UTPSocket* us) :
    UtpPort(INVALID_SOCKET), listener(lr)
{
    UTPDRV_TRACE("Server::Server\r\n");
    utp = us;
    set_utp_callbacks(utp);
    writable = true;
    status = connected;
}

UtpDrv::Server::~Server()
{
    UTPDRV_TRACE("Server::~Server\r\n");
}

ErlDrvSSizeT
UtpDrv::Server::control(unsigned command, const char* buf, ErlDrvSizeT len,
                        char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACE("Server::control\r\n");
    switch (command) {
    case UTP_CLOSE:
        return close(buf, len, rbuf);
    case UTP_SOCKNAME:
        return listener.sockname(buf, len, rbuf);
    case UTP_PEERNAME:
        return peername(buf, len, rbuf);
    case UTP_RECV:
        return encode_error(rbuf, ENOTCONN);
    }
    return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_GENERAL);
}

void
UtpDrv::Server::stop()
{
    UTPDRV_TRACE("Server::stop\r\n");}

void
UtpDrv::Server::incoming()
{
    UTPDRV_TRACE("Server::incoming\r\n");
}

void
UtpDrv::Server::force_close()
{
    UTPDRV_TRACE("Server::force_close\r\n");
    if (utp != 0) {
        MutexLocker lock(utp_mutex);
        UTP_Close(utp);
    }
}

ErlDrvSSizeT
UtpDrv::Server::close(const char* buf, ErlDrvSizeT len, char** rbuf)
{
    UTPDRV_TRACE("Server::close\r\n");
    listener.server_closing(this);
    return UtpPort::close(buf, len, rbuf);
}

void
UtpDrv::Server::do_send_to(const byte* p, size_t len, const sockaddr* to,
                           socklen_t slen)
{
    UTPDRV_TRACE("Server::do_send_to\r\n");
    listener.do_send_to(p, len, to, slen);
}

void
UtpDrv::Server::do_read(const byte* bytes, size_t count)
{
    UTPDRV_TRACE("Server::do_read\r\n");
    UtpPort::do_read(bytes, count);
}

void
UtpDrv::Server::do_write(byte* bytes, size_t count)
{
    UTPDRV_TRACE("Server::do_write\r\n");
    UtpPort::do_write(bytes, count);
}

size_t
UtpDrv::Server::do_get_rb_size()
{
    UTPDRV_TRACE("Server::do_get_rb_size\r\n");
    return UtpPort::do_get_rb_size();
}

void
UtpDrv::Server::do_state_change(int s)
{
    UTPDRV_TRACE("Server::do_state_change\r\n");
    UtpPort::do_state_change(s);
}

void
UtpDrv::Server::do_error(int errcode)
{
    UTPDRV_TRACE("Server::do_error\r\n");
    UtpPort::do_error(errcode);
}

void
UtpDrv::Server::do_overhead(bool send, size_t count, int type)
{
    UtpPort::do_overhead(send, count, type);
}

void
UtpDrv::Server::do_incoming(UTPSocket* utp)
{
    UTPDRV_TRACE("Server::do_incoming\r\n");
}
