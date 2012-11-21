// -------------------------------------------------------------------
//
// utpdrv_listener.cc: Erlang driver listener port for uTP
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

#include <cassert>
#include <unistd.h>
#include "ei.h"
#include "utpdrv_listener.h"
#include "utpdrv_server.h"
#include "locker.h"


using namespace UtpDrv;


UtpDrv::Listener::Listener(Dispatcher& disp, int sock) : Port(disp, sock)
{
}

UtpDrv::Listener::~Listener()
{
}

ErlDrvSSizeT
UtpDrv::Listener::send(const char* buf, ErlDrvSizeT len,
                       char** rbuf, ErlDrvSizeT rlen)
{
    return enotconn(rbuf);
}

ErlDrvSSizeT
UtpDrv::Listener::peername(const char* buf, ErlDrvSizeT len,
                           char** rbuf, ErlDrvSizeT rlen)
{
    return enotconn(rbuf);
}

ErlDrvSSizeT
UtpDrv::Listener::close(const char* buf, ErlDrvSizeT len,
                        char** rbuf, ErlDrvSizeT rlen)
{
    ServerMap::iterator it = servers.begin();
    while (it != servers.end()) {
        it++->second->force_close();
    }
    return Port::close(buf, len, rbuf, rlen);
}

void
UtpDrv::Listener::stop()
{
    topdisp.deselect(udp_sock);
    ::close(udp_sock);
    Port::stop();
}

void
UtpDrv::Listener::do_incoming(UTPSocket* utp)
{
    printf("called Listener::do_incoming\r\n"); fflush(stdout);
    SockAddr addr;
    socklen_t addrlen = sizeof addr;
    UTP_GetPeerName(utp, reinterpret_cast<sockaddr*>(&addr), &addrlen);
    ServerMap::iterator it = servers.find(addr);
    if (it == servers.end()) {
        Server* server = new Server(topdisp, *this, utp);
        ServerMap::value_type val(addr, server);
        std::pair<ServerMap::iterator, bool> p = servers.insert(val);
        it = p.first;
        ErlDrvTermData owner = driver_connected(drv_port);
        ErlDrvPort new_port = topdisp.create_port(owner, server);
        char addrstr[INET6_ADDRSTRLEN];
        unsigned short port;
        sockaddr_to_addrport(addr, addrlen, addrstr, sizeof addrstr, port);
        ErlDrvTermData strdata = reinterpret_cast<ErlDrvTermData>(addrstr);
        ErlDrvTermData term[] = {
            ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("utp_async")),
            ERL_DRV_PORT, driver_mk_port(new_port),
            ERL_DRV_STRING, strdata, strlen(addrstr),
            ERL_DRV_UINT, port,
            ERL_DRV_TUPLE, 2,
            ERL_DRV_TUPLE, 3,
        };
        MutexLocker lock(drv_mutex);
        driver_output_term(drv_port, term, sizeof term/sizeof *term);
    }
    it->second->incoming();
}
