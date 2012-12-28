// -------------------------------------------------------------------
//
// listener.cc: uTP listen port
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

#include "listener.h"
#include "globals.h"
#include "utils.h"
#include "locker.h"
#include "server.h"


using namespace UtpDrv;

UtpDrv::Listener::Listener(int sock, DataDelivery del, long send_timeout) :
    UtpPort(sock, del, send_timeout)
{
    UTPDRV_TRACE("Listener::Listener\r\n");
    sm_mutex = erl_drv_mutex_create(const_cast<char*>("listener"));
}

UtpDrv::Listener::~Listener()
{
    UTPDRV_TRACE("Listener::~Listener\r\n");
    erl_drv_mutex_destroy(sm_mutex);
}

ErlDrvSSizeT
UtpDrv::Listener::control(unsigned command, const char* buf, ErlDrvSizeT len,
                          char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACE("Listener::control\r\n");
    switch (command) {
    case UTP_CLOSE:
        return close(buf, len, rbuf);
    case UTP_SOCKNAME:
        return UtpPort::sockname(buf, len, rbuf);
    case UTP_PEERNAME:
        return peername(buf, len, rbuf);
    }
    return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_GENERAL);
}

void
UtpDrv::Listener::outputv(ErlIOVec&)
{
    send_not_connected();
}

void
UtpDrv::Listener::stop()
{
    UTPDRV_TRACE("Listener::stop\r\n");
    if (main_port) {
        main_port->deselect(udp_sock);
    }
}

void
UtpDrv::Listener::do_send_to(const byte* p, size_t len,
                             const sockaddr* to, socklen_t slen)
{
    UTPDRV_TRACE("Listener::do_send_to\r\n");
    UtpPort::do_send_to(p, len, to, slen);
}

void
UtpDrv::Listener::do_read(const byte* bytes, size_t count)
{
    UTPDRV_TRACE("Listener::do_read\r\n");
    UtpPort::do_read(bytes, count);
}

void
UtpDrv::Listener::do_write(byte* bytes, size_t count)
{
    UTPDRV_TRACE("Listener::do_write\r\n");
    // do nothing
}

size_t
UtpDrv::Listener::do_get_rb_size()
{
    UTPDRV_TRACE("Listener::do_get_rb_size\r\n");
    return UtpPort::do_get_rb_size();
}

void
UtpDrv::Listener::do_state_change(int s)
{
    UTPDRV_TRACE("Listener::do_state_change\r\n");
    UtpPort::do_state_change(s);
}

void
UtpDrv::Listener::do_error(int errcode)
{
    UTPDRV_TRACE("Listener::do_error\r\n");
}

void
UtpDrv::Listener::do_overhead(bool send, size_t count, int type)
{
    UtpPort::do_overhead(send, count, type);
}

void
UtpDrv::Listener::do_incoming(UTPSocket* utp)
{
    UTPDRV_TRACE("Listener::do_incoming\r\n");
    SockAddr addr;
    UTP_GetPeerName(utp, addr, &addr.slen);
    AddrMap::iterator it;
    {
        MutexLocker lock(sm_mutex);
        it = addrs.find(addr);
        if (it == addrs.end()) {
            Server* server = new Server(*this, utp, delivery_type, send_tmout);
            AddrMap::value_type aval(addr, server);
            std::pair<AddrMap::iterator, bool> p = addrs.insert(aval);
            it = p.first;
            ServerMap::value_type sval(server, addr);
            servers.insert(sval);
            ErlDrvTermData owner = driver_connected(port);
            ErlDrvPort new_port = create_port(owner, server);
            if (!server->set_port(new_port)) {
                driver_failure_atom(new_port,
                                    const_cast<char*>("port_data_lock_failed"));
                addrs.erase(addr);
                return;
            }
            ErlDrvTermData term[sizeof(in6_addr) + 12] = {
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("utp_async")),
                ERL_DRV_PORT, driver_mk_port(new_port),
            };
            int j = 4;
            sockaddr* sa = reinterpret_cast<sockaddr*>(&addr.addr);
            unsigned short addrport;
            int addr_tuple_size;
            if (sa->sa_family == AF_INET) {
                sockaddr_in* sa_in = reinterpret_cast<sockaddr_in*>(sa);
                addrport = ntohs(sa_in->sin_port);
                uint8_t* pb = reinterpret_cast<uint8_t*>(&sa_in->sin_addr);
                addr_tuple_size = sizeof(in_addr);
                for (int i = 0; i < addr_tuple_size; ++i, j+=2) {
                    term[j] = ERL_DRV_UINT;
                    term[j+1] = *pb++;
                }
            } else {
                sockaddr_in6* sa6 = reinterpret_cast<sockaddr_in6*>(sa);
                addrport = ntohs(sa6->sin6_port);
                uint16_t* pw = reinterpret_cast<uint16_t*>(&sa6->sin6_addr);
                addr_tuple_size = sizeof(in6_addr)/sizeof(*pw);
                for (int i = 0; i < addr_tuple_size; ++i, j+=2) {
                    term[j] = ERL_DRV_UINT;
                    term[j+1] = *pw++;
                }
            }
            term[j++] = ERL_DRV_TUPLE;
            term[j++] = addr_tuple_size;
            term[j++] = ERL_DRV_UINT;
            term[j++] = addrport;
            term[j++] = ERL_DRV_TUPLE;
            term[j++] = 2;
            term[j++] = ERL_DRV_TUPLE;
            term[j++] = 3;
            int term_size = 2*addr_tuple_size + 12;
            MutexLocker lock(drv_mutex);
            driver_output_term(port, term, term_size);
        }
    }
    it->second->incoming();
}

ErlDrvSSizeT
UtpDrv::Listener::peername(const char* buf, ErlDrvSizeT len, char** rbuf)
{
    UTPDRV_TRACE("Listener::peername\r\n");
    return encode_error(rbuf, ENOTCONN);
}

void
UtpDrv::Listener::server_closing(Server* svr)
{
    MutexLocker lock(sm_mutex);
    ServerMap::iterator it = servers.find(svr);
    if (it != servers.end()) {
        addrs.erase(it->second);
        servers.erase(svr);
    }
}

ErlDrvSSizeT
UtpDrv::Listener::close(const char* buf, ErlDrvSizeT len, char** rbuf)
{
    UTPDRV_TRACE("Listener::close\r\n");
    return UtpPort::close(buf, len, rbuf);
}
