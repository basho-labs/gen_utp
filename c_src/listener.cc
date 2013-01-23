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
#include "libutp/utp.h"
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
    queue_mutex = erl_drv_mutex_create(const_cast<char*>("queue_mutex"));
}

UtpDrv::Listener::~Listener()
{
    UTPDRV_TRACER << "Listener::~Listener " << this << UTPDRV_TRACE_ENDL;
    erl_drv_mutex_destroy(queue_mutex);
}

ErlDrvSSizeT
UtpDrv::Listener::control(unsigned command, const char* buf, ErlDrvSizeT len,
                            char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "Listen::control " << this << UTPDRV_TRACE_ENDL;
    switch (command) {
    case UTP_ACCEPT:
        return accept(buf, len, rbuf, rlen);
    case UTP_CANCEL_ACCEPT:
        return cancel_accept();
    }
    return SocketHandler::control(command, buf, len, rbuf, rlen);
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
    unsigned char buf[512];
    SockAddr from;
    int len = recvfrom(udp_sock, buf, sizeof buf, 0, from, &from.slen);
    if (len <= 0) {
        return;
    }
    // if we have nobody accepting connections, just drop the message
    MutexLocker qlock(queue_mutex);
    size_t qsize = acceptor_queue.size();
    if (qsize == 0) {
        return;
    }
    int sock;
    if (open_udp_socket(sock, my_addr, true) < 0) {
        return;
    }
    for (;;) {
        int res = connect(sock, from, from.slen);
        if (res == 0) {
            break;
        } else if (res < 0 && errno != EINTR) {
            int err = errno;
            ::close(sock);
            if (sockopts.active != ACTIVE_FALSE) {
                ErlDrvTermData term[] = {
                    ERL_DRV_ATOM,
                    driver_mk_atom(const_cast<char*>("utp_error")),
                    ERL_DRV_PORT, driver_mk_port(port),
                    ERL_DRV_ATOM, driver_mk_atom(erl_errno_id(err)),
                    ERL_DRV_TUPLE, 3
                };
                MutexLocker lock(drv_mutex);
                driver_output_term(port, term, sizeof term/sizeof *term);
            }
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
        ErlDrvTermData new_port_owner = driver_connected(port);
        Binary ref;
        Acceptor& acc = acceptor_queue.front();
        new_port_owner = acc.caller;
        ref.swap(acc.ref);
        ErlDrvPort new_port = create_port(new_port_owner, server);
        server->set_port(new_port);
        MainHandler::start_input(sock, server);
        ErlDrvTermData term[sizeof(in6_addr) + 12] = {
            ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("utp_async")),
            ERL_DRV_PORT, driver_mk_port(new_port),
        };
        int index = 4;
        if (ref) {
            term[index++] = ERL_DRV_EXT2TERM;
            term[index++] = ref;
            term[index++] = ref.size();
            term[index++] = ERL_DRV_TUPLE;
            term[index++] = 3;
            driver_send_term(port, new_port_owner, term, index);
        } else {
            sockaddr* sa = from;
            unsigned short addrport;
            int addr_tuple_size;
            if (sa->sa_family == AF_INET) {
                sockaddr_in* sa_in = reinterpret_cast<sockaddr_in*>(sa);
                addrport = ntohs(sa_in->sin_port);
                uint8_t* pb = reinterpret_cast<uint8_t*>(&sa_in->sin_addr);
                addr_tuple_size = sizeof(in_addr);
                for (int i = 0; i < addr_tuple_size; ++i, index+=2) {
                    term[index] = ERL_DRV_UINT;
                    term[index+1] = *pb++;
                }
            } else {
                sockaddr_in6* sa6 = reinterpret_cast<sockaddr_in6*>(sa);
                addrport = ntohs(sa6->sin6_port);
                uint16_t* pw = reinterpret_cast<uint16_t*>(&sa6->sin6_addr);
                addr_tuple_size = sizeof(in6_addr)/sizeof(*pw);
                for (int i = 0; i < addr_tuple_size; ++i, index+=2) {
                    term[index] = ERL_DRV_UINT;
                    term[index+1] = *pw++;
                }
            }
            term[index++] = ERL_DRV_TUPLE;
            term[index++] = addr_tuple_size;
            term[index++] = ERL_DRV_UINT;
            term[index++] = addrport;
            term[index++] = ERL_DRV_TUPLE;
            term[index++] = 2;
            term[index++] = ERL_DRV_TUPLE;
            term[index++] = 3;
            MutexLocker lock(drv_mutex);
            driver_output_term(port, term, index);
        }
        acceptor_queue.pop_front();
    } else {
        ::close(sock);
        delete server;
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

ErlDrvSSizeT
UtpDrv::Listener::accept(const char* buf, ErlDrvSizeT len,
                         char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "Listener::accept " << this << UTPDRV_TRACE_ENDL;
    Acceptor acc;
    if (len != 0) {
        try {
            EiDecoder decoder(buf, len);
            int type, size;
            decoder.type(type, size);
            if (type != ERL_BINARY_EXT) {
                return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
            }
            acc.ref.decode(decoder, size);
        } catch (const EiError&) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
    }
    acc.caller = driver_caller(port);
    {
        MutexLocker qlock(queue_mutex);
        acceptor_queue.push_back(acc);
    }

    EiEncoder encoder;
    encoder.atom("ok");
    ErlDrvBinary** binptr = reinterpret_cast<ErlDrvBinary**>(rbuf);
    return encoder.copy_to_binary(binptr, rlen);
}

ErlDrvSSizeT
UtpDrv::Listener::cancel_accept()
{
    UTPDRV_TRACER << "Listener::cancel_accept " << this << UTPDRV_TRACE_ENDL;
    ErlDrvTermData caller = driver_caller(port);
    MutexLocker qlock(queue_mutex);
    AcceptorQueue::iterator it = acceptor_queue.begin();
    while (it != acceptor_queue.end()) {
        if (it->caller == caller) {
            acceptor_queue.erase(it);
            break;
        }
        ++it;
    }
    return 0;
}
