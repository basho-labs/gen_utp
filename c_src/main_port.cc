// -------------------------------------------------------------------
//
// main_port.cc: primary port for uTP driver
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

#include "main_port.h"
#include "globals.h"
#include "utils.h"
#include "locker.h"
#include "libutp/utp.h"
#include "utp_port.h"
#include "client.h"
#include "listener.h"


using namespace UtpDrv;

const unsigned long timeout_check = 100;

ErlDrvMutex* UtpDrv::MainPort::map_mutex = 0;
UtpDrv::MainPort::FdMap UtpDrv::MainPort::fdmap;
UtpDrv::MainPort::MonMap UtpDrv::MainPort::monmap;

UtpDrv::MainPort::MainPort(ErlDrvPort p) : port(p)
{
    owner = driver_connected(port);
    set_port_control_flags(port, PORT_CONTROL_FLAG_BINARY);
}

UtpDrv::MainPort::~MainPort()
{
    port = 0;
}

int
UtpDrv::MainPort::driver_init()
{
    DBGOUT("MainPort::driver_init\r\n");
    utp_mutex = erl_drv_mutex_create(const_cast<char*>("utp"));
    drv_mutex = erl_drv_mutex_create(const_cast<char*>("drv"));
    map_mutex = erl_drv_mutex_create(const_cast<char*>("utpmap"));
    return 0;
}

void
UtpDrv::MainPort::driver_finish()
{
    DBGOUT("MainPort::driver_finish\r\n");
    erl_drv_mutex_destroy(map_mutex);
    erl_drv_mutex_destroy(drv_mutex);
    erl_drv_mutex_destroy(utp_mutex);
}

void
UtpDrv::MainPort::check_utp_timeouts() const
{
    MutexLocker lock(utp_mutex);
    UTP_CheckTimeouts();
    driver_set_timer(port, timeout_check);
}

ErlDrvSSizeT
UtpDrv::MainPort::control(unsigned command, const char* buf, ErlDrvSizeT len,
                          char** rbuf, ErlDrvSizeT rlen)
{
    DBGOUT("MainPort::control\r\n");
    switch (command) {
    case UTP_CONNECT_START:
        return connect_start(buf, len, rbuf, rlen);
        break;
    case UTP_LISTEN:
        return listen(buf, len, rbuf, rlen);
        break;
    default:
        return encode_error(rbuf, "enotsup");
        break;
    }
}

void
UtpDrv::MainPort::start()
{
    DBGOUT("MainPort::start\r\n");
    driver_set_timer(port, timeout_check);
    main_port = this;
}

void
UtpDrv::MainPort::stop()
{
    DBGOUT("MainPort::stop\r\n");
    driver_cancel_timer(port);
    main_port = 0;
}

void
UtpDrv::MainPort::ready_input(long fd)
{
    DBGOUT("MainPort::ready_input\r\n");
    UtpPort* up = 0;
    {
        MutexLocker lock(map_mutex);
        FdMap::iterator it = fdmap.find(fd);
        if (it != fdmap.end()) {
            up = it->second;
        }
    }
    if (up != 0) {
        up->input_ready();
    }
}

void
UtpDrv::MainPort::process_exit(ErlDrvMonitor* monitor)
{
    DBGOUT("MainPort::process_exit\r\n");
}

ErlDrvPort
UtpDrv::MainPort::drv_port() const
{
    DBGOUT("MainPort::drv_port\r\n");
    return port;
}

void
UtpDrv::MainPort::deselect(int fd) const
{
    DBGOUT("MainPort::deselect\r\n");
    if (port != 0) {
        driver_select(port, reinterpret_cast<ErlDrvEvent>(fd),
                      ERL_DRV_READ|ERL_DRV_USE, 0);
    }
    MutexLocker lock(map_mutex);
    fdmap.erase(fd);
}

void
UtpDrv::MainPort::add_monitor(const ErlDrvMonitor& mon, Handler* h)
{
    DBGOUT("MainPort::add_monitor\r\n");
    MutexLocker lock(map_mutex);
    MonMap::value_type val(mon, h);
    monmap.insert(val);
}

void
UtpDrv::MainPort::del_monitor(ErlDrvPort port, ErlDrvMonitor& mon)
{
    DBGOUT("MainPort::del_monitor\r\n");
    MutexLocker lock(map_mutex);
    monmap.erase(mon);
    driver_demonitor_process(port, &mon);
}

ErlDrvSSizeT
UtpDrv::MainPort::connect_start(const char* buf, ErlDrvSizeT len,
                                char** rbuf, ErlDrvSizeT rlen)
{
    DBGOUT("MainPort::connect_start\r\n");
    int arity, type, size;
    ErlDrvBinary* from = 0;
    long bin_size;
    char addrstr[INET6_ADDRSTRLEN];
    unsigned long addrport;
    try {
        EiDecoder decoder(buf, len);
        decoder.tuple_header(arity);
        if (arity != 4) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        decoder.string(addrstr);
        decoder.ulong(addrport);
        // TODO: skip connect options for now...
        decoder.skip();
        decoder.type(type, size);
        if (type != ERL_BINARY_EXT) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        from = driver_alloc_binary(size);
        decoder.binary(from->orig_bytes, bin_size);
    } catch (const EiError&) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    SockAddr addr;
    try {
        SockAddr sa(addrstr, addrport);
        addr = sa;
    } catch (const BadSockAddr&) {
        driver_free_binary(from);
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    int udp_sock;
    int err = open_udp_socket(udp_sock);
    if (err != 0) {
        driver_free_binary(from);
        return encode_error(rbuf, err);
    }
    Client* client = new Client(udp_sock);
    ErlDrvTermData caller = driver_caller(port);
    ErlDrvPort new_port = create_port(caller, client);
    if (!client->set_port(new_port)) {
        driver_failure_atom(new_port,
                            const_cast<char*>("port_data_lock_failed"));
        driver_free_binary(from);
        return encode_error(rbuf, "noproc");
    }
    {
        FdMap::value_type val(udp_sock, client);
        MutexLocker lock(map_mutex);
        fdmap.insert(val);
    }
    ErlDrvEvent ev = reinterpret_cast<ErlDrvEvent>(udp_sock);
    driver_select(port, ev, ERL_DRV_READ|ERL_DRV_USE, 1);
    client->connect_to(addr);
    {
        ErlDrvTermData ext = reinterpret_cast<ErlDrvTermData>(from->orig_bytes);
        ErlDrvTermData term[] = {
            ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("ok")),
            ERL_DRV_PORT, driver_mk_port(new_port),
            ERL_DRV_EXT2TERM, ext, bin_size,
            ERL_DRV_TUPLE, 3,
        };
        driver_send_term(port, caller, term, sizeof term/sizeof *term);
        driver_free_binary(from);
    }
    return 0;
}

ErlDrvSSizeT
UtpDrv::MainPort::listen(const char* buf, ErlDrvSizeT len,
                         char** rbuf, ErlDrvSizeT rlen)
{
    DBGOUT("MainPort::listen\r\n");
    int arity, type, size;
    unsigned long addrport;
    long bin_size;
    ErlDrvBinary* from = 0;
    try {
        EiDecoder decoder(buf, len);
        decoder.tuple_header(arity);
        if (arity != 2) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        decoder.ulong(addrport);
        decoder.type(type, size);
        if (type != ERL_BINARY_EXT) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        from = driver_alloc_binary(size);
        decoder.binary(from->orig_bytes, bin_size);
    } catch (const EiError&) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    ErlDrvTermData ext = reinterpret_cast<ErlDrvTermData>(from->orig_bytes);
    ErlDrvTermData caller = driver_caller(port);
    int udp_sock;
    int err = open_udp_socket(udp_sock, addrport);
    if (err != 0) {
        ErlDrvTermData term[] = {
            ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("error")),
            ERL_DRV_ATOM, driver_mk_atom(erl_errno_id(err)),
            ERL_DRV_EXT2TERM, ext, bin_size,
            ERL_DRV_TUPLE, 3,
        };
        driver_send_term(port, caller, term, sizeof term/sizeof *term);
    } else {
        Listener* listener = new Listener(udp_sock);
        ErlDrvEvent ev = reinterpret_cast<ErlDrvEvent>(udp_sock);
        driver_select(port, ev, ERL_DRV_READ|ERL_DRV_USE, 1);
        ErlDrvPort new_port = create_port(caller, listener);
        if (!listener->set_port(new_port)) {
            driver_failure_atom(new_port,
                                const_cast<char*>("port_data_lock_failed"));
            driver_free_binary(from);
            return encode_error(rbuf, "noproc");
        }
        ErlDrvTermData term[] = {
            ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("ok")),
            ERL_DRV_PORT, driver_mk_port(new_port),
            ERL_DRV_EXT2TERM, ext, bin_size,
            ERL_DRV_TUPLE, 3,
        };
        driver_send_term(port, caller, term, sizeof term/sizeof *term);
        FdMap::value_type val(udp_sock, listener);
        MutexLocker lock(map_mutex);
        fdmap.insert(val);
    }
    driver_free_binary(from);
    return 0;
}
