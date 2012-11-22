// -------------------------------------------------------------------
//
// utpdrv_server.cc: Erlang driver server port for uTP
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

#include "ei.h"
#include "utpdrv_server.h"
#include "locker.h"


using namespace UtpDrv;

UtpDrv::Server::Server(Dispatcher& disp, Listener& lr, UTPSocket* u) :
    Port(disp, INVALID_SOCKET), lstnr(lr)
{
    utp = u;
    status = connect_pending;
}

UtpDrv::Server::~Server()
{
}

ErlDrvSSizeT
UtpDrv::Server::close(const char* buf, ErlDrvSizeT len,
                      char** rbuf, ErlDrvSizeT rlen)
{
    printf("called Server::close\r\n"); fflush(stdout);
    lstnr.server_close(this);
    return Port::close(buf, len, rbuf, rlen);
}

void
UtpDrv::Server::incoming()
{
    printf("called Server::incoming\r\n"); fflush(stdout);
    set_callbacks();
    writable = true;
}

void
UtpDrv::Server::force_close()
{
    printf("called Server::force_close\r\n"); fflush(stdout);
    if (utp != 0) {
        MutexLocker lock(utp_mutex);
        UTP_Close(utp);
    }
}
