// -------------------------------------------------------------------
//
// main_handler.cc: handler for primary uTP driver port
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

#include <sys/time.h>
#include "main_handler.h"
#include "globals.h"
#include "locker.h"
#include "libutp/utp.h"
#include "utp_handler.h"
#include "client.h"
#include "listener.h"


using namespace UtpDrv;

const unsigned long timeout_check = 10;

UtpDrv::MainHandler* UtpDrv::MainHandler::main_handler = 0;

UtpDrv::MainHandler::MainHandler(ErlDrvPort p) :
    Handler(p), delegatee(0), map_mutex(0)
{
}

UtpDrv::MainHandler::MainHandler(Handler* dg) :
    Handler(0), delegatee(dg), map_mutex(0)
{
}

UtpDrv::MainHandler::~MainHandler()
{
    delete delegatee;
}

int
UtpDrv::MainHandler::driver_init()
{
    UTPDRV_TRACER << "MainHandler::driver_init" << UTPDRV_TRACE_ENDL;
    utp_mutex = erl_drv_mutex_create(const_cast<char*>("utp"));
    return 0;
}

void
UtpDrv::MainHandler::driver_finish()
{
    UTPDRV_TRACER << "MainHandler::driver_finish" << UTPDRV_TRACE_ENDL;
    erl_drv_mutex_destroy(utp_mutex);
}

void
UtpDrv::MainHandler::check_utp_timeouts() const
{
    if (main_handler != 0) {
        MutexLocker lock(utp_mutex);
        UTP_CheckTimeouts();
        driver_set_timer(port, timeout_check);
    }
}

ErlDrvSSizeT
UtpDrv::MainHandler::control(unsigned command, const char* buf, ErlDrvSizeT len,
                             char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "MainHandler::control " << this << UTPDRV_TRACE_ENDL;
    if (delegatee != 0) {
        return delegatee->control(command, buf, len, rbuf, rlen);
    } else {
        switch (command) {
        case UTP_LISTEN:
            return listen(buf, len, rbuf, rlen);
            break;
        case UTP_CONNECT_START:
            return connect_start(buf, len, rbuf, rlen);
            break;
        default:
            return encode_error(rbuf, rlen, "enotsup");
            break;
        }
    }
}

void
UtpDrv::MainHandler::start()
{
    UTPDRV_TRACER << "MainHandler::start " << this << UTPDRV_TRACE_ENDL;
    MutexLocker lock(utp_mutex);
    if (main_handler == 0) {
        UTPDRV_TRACER << "main_handler set to " << this << UTPDRV_TRACE_ENDL;
        main_handler = this;
        driver_set_timer(port, timeout_check);
        map_mutex = erl_drv_mutex_create(const_cast<char*>("utpmap"));
    }
}

void
UtpDrv::MainHandler::stop()
{
    UTPDRV_TRACER << "MainHandler::stop " << this << UTPDRV_TRACE_ENDL;
    if (delegatee != 0) {
        delegatee->stop();
    }
    MutexLocker lock(utp_mutex);
    if (main_handler == this) {
        main_handler = 0;
        driver_cancel_timer(port);
        erl_drv_mutex_destroy(map_mutex);
    }
}

void
UtpDrv::MainHandler::ready_input(long fd)
{
    SocketHandler* hndlr = 0;
    {
        MutexLocker lock(map_mutex);
        FdMap::iterator it = fdmap.find(fd);
        if (it != fdmap.end()) {
            hndlr = it->second;
        }
    }
    if (hndlr != 0) {
        hndlr->input_ready();
    }
}

void
UtpDrv::MainHandler::outputv(ErlIOVec& iovec)
{
    UTPDRV_TRACER << "MainHandler::outputv" << UTPDRV_TRACE_ENDL;
    if (delegatee != 0) {
        delegatee->outputv(iovec);
    }
}

void
UtpDrv::MainHandler::process_exit(ErlDrvMonitor* monitor)
{
    UTPDRV_TRACER << "MainHandler::process_exit" << UTPDRV_TRACE_ENDL;
    Handler* h = 0;
    ErlDrvTermData proc;
    {
        MutexLocker lock(map_mutex);
        MonMap::iterator it = mon_map.find(*monitor);
        if (it != mon_map.end()) {
            h = it->second;
            mon_map.erase(it);
            proc = driver_get_monitored_process(port, monitor);
            proc_mon_map.erase(proc);
        }
    }
    if (h != 0) {
        h->process_exited(monitor, proc);
    }
}

ErlDrvPort
UtpDrv::MainHandler::drv_port()
{
    return main_handler->port;
}

void
UtpDrv::MainHandler::start_input(int fd, SocketHandler* handler)
{
    UTPDRV_TRACER << "MainHandler::start_input for socket " << fd
                  << UTPDRV_TRACE_ENDL;
    if (main_handler != 0) {
        main_handler->select(fd, handler);
    }
}

void
UtpDrv::MainHandler::stop_input(int fd)
{
    UTPDRV_TRACER << "MainHandler::stop_input for socket " << fd
                  << UTPDRV_TRACE_ENDL;
    if (main_handler != 0) {
        main_handler->deselect(fd);
    }
}

bool
UtpDrv::MainHandler::add_monitor(ErlDrvTermData proc, Handler* h)
{
    UTPDRV_TRACER << "MainHandler::add_monitor" << UTPDRV_TRACE_ENDL;
    return main_handler != 0 ? main_handler->add_mon(proc, h) : false;
}

bool
UtpDrv::MainHandler::add_mon(ErlDrvTermData proc, Handler* h)
{
    UTPDRV_TRACER << "MainHandler::add_mon " << this << UTPDRV_TRACE_ENDL;
    MutexLocker lock(map_mutex);
    ErlDrvMonitor mon;
    bool result = (driver_monitor_process(port, proc, &mon) == 0);
    if (result) {
        MonMap::value_type v1(mon, h);
        ProcMonMap::value_type v2(proc, mon);
        mon_map.insert(v1);
        proc_mon_map.insert(v2);
    }
    return result;
}

void
UtpDrv::MainHandler::del_monitor(ErlDrvTermData proc)
{
    UTPDRV_TRACER << "MainHandler::del_monitor" << UTPDRV_TRACE_ENDL;
    if (main_handler != 0) {
        main_handler->del_mon(proc);
    }
}

void
UtpDrv::MainHandler::del_monitors(Handler* h)
{
    UTPDRV_TRACER << "MainHandler::del_monitors" << UTPDRV_TRACE_ENDL;
    if (main_handler != 0) {
        main_handler->del_mons(h);
    }
}

void
UtpDrv::MainHandler::del_mon(ErlDrvTermData proc)
{
    UTPDRV_TRACER << "MainHandler::del_mon " << this << UTPDRV_TRACE_ENDL;
    MutexLocker lock(map_mutex);
    ProcMonMap::iterator it = proc_mon_map.find(proc);
    if (it != proc_mon_map.end()) {
        driver_demonitor_process(port, &it->second);
        mon_map.erase(it->second);
        proc_mon_map.erase(it);
    }
}

void
UtpDrv::MainHandler::del_mons(Handler* h)
{
    if (h != this) {
        UTPDRV_TRACER << "MainHandler::del_mons " << this << UTPDRV_TRACE_ENDL;
        MutexLocker lock(map_mutex);
        MonMap::iterator it = mon_map.begin();
        while (it != mon_map.end()) {
            if (it->second == h) {
                ProcMonMap::iterator itp = proc_mon_map.begin();
                while (itp != proc_mon_map.end()) {
                    if (driver_compare_monitors(&it->first, &itp->second) == 0) {
                        driver_demonitor_process(port, &it->first);
                        proc_mon_map.erase(itp++);
                    } else {
                        ++itp;
                    }
                }
                mon_map.erase(it++);
            } else {
                ++it;
            }
        }
    }
}

ErlDrvSSizeT
UtpDrv::MainHandler::connect_start(const char* buf, ErlDrvSizeT len,
                                   char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "MainHandler::connect_start " << this << UTPDRV_TRACE_ENDL;
    Binary binopts;
    char addrstr[INET6_ADDRSTRLEN];
    unsigned long addrport;
    try {
        EiDecoder decoder(buf, len);
        int arity, type, size;
        decoder.tuple_header(arity);
        if (arity != 3) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        decoder.string(addrstr);
        decoder.ulongval(addrport);
        decoder.type(type, size);
        if (type != ERL_BINARY_EXT) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        binopts.decode(decoder, size);
    } catch (const EiError&) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    SockAddr addr;
    try {
        addr.from_addrport(addrstr, addrport);
    } catch (const BadSockAddr&) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    SocketHandler::SockOpts opts;
    opts.decode(binopts);
    int udp_sock, err;
    if (opts.fd != INVALID_SOCKET) {
        udp_sock = opts.fd;
        SockAddr fdaddr;
        if (getsockname(udp_sock, fdaddr, &fdaddr.slen) < 0) {
            err = errno;
        } else {
            err = 0;
        }
    } else if (opts.addr_set) {
        err = SocketHandler::open_udp_socket(udp_sock, opts.addr);
    } else if (opts.inet6) {
        SockAddr in6_any("::", 0);
        err = SocketHandler::open_udp_socket(udp_sock, in6_any);
    } else {
        err = SocketHandler::open_udp_socket(udp_sock, opts.port);
    }
    if (err != 0) {
        return encode_error(rbuf, rlen, erl_errno_id(err));
    } else {
        Client* client = new Client(udp_sock, opts);
        delegatee = client;
        client->set_port(port);
        port = 0;
        client->connect_to(addr);
        return encode_atom(rbuf, rlen, "ok");
    }
    return 0;
}

ErlDrvSSizeT
UtpDrv::MainHandler::listen(const char* buf, ErlDrvSizeT len,
                            char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "MainHandler::listen " << this << UTPDRV_TRACE_ENDL;
    Binary binopts;
    try {
        EiDecoder decoder(buf, len);
        int type, size;
        decoder.type(type, size);
        if (type != ERL_BINARY_EXT) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        binopts.decode(decoder, size);
    } catch (const EiError&) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    SocketHandler::SockOpts opts;
    opts.decode(binopts);
    int udp_sock, err;
    if (opts.fd != INVALID_SOCKET) {
        udp_sock = opts.fd;
        SockAddr fdaddr;
        if (getsockname(udp_sock, fdaddr, &fdaddr.slen) < 0) {
            err = errno;
        } else {
            err = 0;
        }
    } else if (opts.addr_set) {
        err = SocketHandler::open_udp_socket(udp_sock, opts.addr, true);
    } else if (opts.inet6) {
        SockAddr in6_any("::", 0);
        err = SocketHandler::open_udp_socket(udp_sock, in6_any, true);
    } else {
        err = SocketHandler::open_udp_socket(udp_sock, opts.port, true);
    }
    if (err != 0) {
        return encode_error(rbuf, rlen, erl_errno_id(err));
    } else {
        delegatee = new Listener(udp_sock, opts);
        delegatee->set_port(port);
        port = 0;
        return encode_atom(rbuf, rlen, "ok");
    }
}

void
UtpDrv::MainHandler::select(int fd, SocketHandler* handler)
{
    UTPDRV_TRACER << "MainHandler::select for socket " << fd
                  << ", handler " << handler << UTPDRV_TRACE_ENDL;
    if (port != 0) {
        driver_select(port, reinterpret_cast<ErlDrvEvent>(fd),
                      ERL_DRV_READ|ERL_DRV_USE, 1);
    }
    FdMap::value_type val(fd, handler);
    MutexLocker lock(map_mutex);
    fdmap.insert(val);
}

void
UtpDrv::MainHandler::deselect(int& fd)
{
    UTPDRV_TRACER << "MainHandler::deselect socket " << fd << UTPDRV_TRACE_ENDL;
    if (fd != INVALID_SOCKET) {
        if (port != 0) {
            driver_select(port, reinterpret_cast<ErlDrvEvent>(fd),
                          ERL_DRV_READ|ERL_DRV_USE, 0);
        }
        MutexLocker lock(map_mutex);
        fdmap.erase(fd);
    }
}
