// -------------------------------------------------------------------
//
// listener.cc: uTP listen port
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

#include <unistd.h>
#include "listener.h"
#include "globals.h"
#include "main_handler.h"
#include "utils.h"
#include "locker.h"
#include "server.h"


using namespace UtpDrv;

UtpDrv::Listener::Listener(int sock, const SockOpts& so) :
    SocketHandler(sock, so)
{
    UTPDRV_TRACER << "Listener::Listener " << this
                  << ", socket " << sock << UTPDRV_TRACE_ENDL;
    if (getsockname(udp_sock, my_addr, &my_addr.slen) < 0) {
        throw SocketFailure(errno);
    }
}

UtpDrv::Listener::~Listener()
{
    UTPDRV_TRACER << "Listener::~Listener " << this << UTPDRV_TRACE_ENDL;
}

void
UtpDrv::Listener::outputv(ErlIOVec&)
{
    UTPDRV_TRACER << "Listener::outputv " << this << UTPDRV_TRACE_ENDL;
    send_not_connected(port);
}

void
UtpDrv::Listener::stop()
{
    UTPDRV_TRACER << "Listener::stop " << this << UTPDRV_TRACE_ENDL;
    MainHandler::stop_input(udp_sock);
    delete this;
}

void
UtpDrv::Listener::process_exit(ErlDrvMonitor* mon)
{
    UTPDRV_TRACER << "Listener::process_exit " << this << UTPDRV_TRACE_ENDL;
}

void
UtpDrv::Listener::input_ready()
{
    UTPDRV_TRACER << "Listener::input_ready " << this << UTPDRV_TRACE_ENDL;
    // TODO: when recv buffer sizes can be varied, the following buffer will
    // need to come from a pool
    byte buf[8192];
    SockAddr from;
    int len = recvfrom(udp_sock, buf, sizeof buf, 0, from, &from.slen);
    if (len > 0) {
        int sock;
        if (open_udp_socket(sock, my_addr, true) < 0) {
            return;
        }
        for (;;) {
            int res = connect(sock, from, from.slen);
            if (res == 0) {
                break;
            } else if (res < 0 && res != EINTR) {
                ::close(sock);
                return;
            }
        }
        Server* server = new Server(sock, sockopts);
        bool is_utp;
        {
            MutexLocker lock(utp_mutex);
            is_utp = UTP_IsIncomingUTP(&UtpHandler::utp_incoming,
                                       &UtpHandler::send_to, server,
                                       buf, len, from, from.slen);
        }
        if (is_utp) {
            ErlDrvTermData owner = driver_connected(port);
            ErlDrvPort new_port = create_port(owner, server);
            server->set_port(new_port);
            MainHandler::start_input(sock, server);
            ErlDrvTermData term[sizeof(in6_addr) + 12] = {
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("utp_async")),
                ERL_DRV_PORT, driver_mk_port(new_port),
            };
            int j = 4;
            sockaddr* sa = from;
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
        } else {
            delete server;
        }
    }
}

void
UtpDrv::Listener::do_write(byte* bytes, size_t count)
{
    UTPDRV_TRACER << "Listener::do_write " << this << UTPDRV_TRACE_ENDL;
    // do nothing
}

void
UtpDrv::Listener::do_incoming(UTPSocket* utp)
{
    UTPDRV_TRACER << "Listener::do_incoming " << this << UTPDRV_TRACE_ENDL;
}

ErlDrvSSizeT
UtpDrv::Listener::close(const char* buf, ErlDrvSizeT len,
                        char** rbuf, ErlDrvSizeT rlen)
{
    const char* retval = "ok";
    EiEncoder encoder;
    encoder.atom(retval);
    ErlDrvBinary** binptr = reinterpret_cast<ErlDrvBinary**>(rbuf);
    return encoder.copy_to_binary(binptr, rlen);
}

ErlDrvSSizeT
UtpDrv::Listener::peername(const char* buf, ErlDrvSizeT len,
                           char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "Listener::peername " << this << UTPDRV_TRACE_ENDL;
    return encode_error(rbuf, rlen, ENOTCONN);
}
