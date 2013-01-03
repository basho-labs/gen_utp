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

UtpDrv::Server::Server(int sock, DataDelivery del, long send_timeout) :
    UtpPort(sock, del, send_timeout)
{
    UTPDRV_TRACE("Server::Server\r\n");
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
    case UTP_SOCKNAME:
        return sockname(buf, len, rbuf, rlen);
    case UTP_PEERNAME:
        return peername(buf, len, rbuf, rlen);
    case UTP_CLOSE:
        return close(buf, len, rbuf, rlen);
    case UTP_SETOPTS:
        return setopts(buf, len, rbuf, rlen);
    case UTP_CANCEL_SEND:
        return cancel_send();
    }
    return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_GENERAL);
}

void
UtpDrv::Server::do_send_to(const byte* p, size_t len,
                           const sockaddr* to, socklen_t slen)
{
    UTPDRV_TRACE("Server::do_send_to\r\n");
    int index = 0;
    for (;;) {
        ssize_t count = send(udp_sock, p+index, len-index, 0);
        if (count == ssize_t(len-index)) {
            break;
        } else if (count < 0 && count != EINTR &&
                   count != EAGAIN && count != EWOULDBLOCK) {
            close_utp();
        } else {
            index += count;
        }
    }
}


void
UtpDrv::Server::do_incoming(UTPSocket* utp_sock)
{
    UTPDRV_TRACE("Server::do_incoming\r\n");
    if (utp == 0) {
        utp = utp_sock;
        set_utp_callbacks(utp);
        writable = true;
        status = connected;
    }
}
