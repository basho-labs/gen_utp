// -------------------------------------------------------------------
//
// utpdrv_dispatcher.cc: uTP driver dispatcher
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

#include <fcntl.h>
#include <unistd.h>
#include "utpdrv_dispatcher.h"
#include "utpdrv_port.h"
#include "utpdrv_listener.h"
#include "ei.h"
#include "libutp/utp.h"
#include "locker.h"

using namespace UtpDrv;

const unsigned long timeout_check = 100;

char* UtpDrv::Dispatcher::drv_name = const_cast<char*>("utpdrv");
Dispatcher::FdMap UtpDrv::Dispatcher::fdmap;
ErlDrvMutex* UtpDrv::Dispatcher::utp_mutex = 0;
ErlDrvMutex* UtpDrv::Dispatcher::drv_mutex = 0;
ErlDrvMutex* UtpDrv::Dispatcher::map_mutex = 0;


UtpDrv::Dispatcher::Dispatcher(ErlDrvPort port) : drv_port(port)
{
    if (drv_port != reinterpret_cast<ErlDrvPort>(-1)) {
        drv_owner = driver_caller(drv_port);
        set_port_control_flags(drv_port, PORT_CONTROL_FLAG_BINARY);
    }
}

UtpDrv::Dispatcher::~Dispatcher()
{
    drv_port = reinterpret_cast<ErlDrvPort>(-1);
}

void*
UtpDrv::Dispatcher::operator new(size_t s)
{
    return driver_alloc(s);
}

void
UtpDrv::Dispatcher::operator delete(void* p)
{
    driver_free(p);
}

void
UtpDrv::Dispatcher::start()
{
    driver_set_timer(drv_port, timeout_check);
}

void
UtpDrv::Dispatcher::stop()
{
    driver_cancel_timer(drv_port);
}

void
UtpDrv::Dispatcher::check_timeouts()
{
    MutexLocker lock(utp_mutex);
    UTP_CheckTimeouts();
    driver_set_timer(drv_port, timeout_check);
}

int
UtpDrv::Dispatcher::init()
{
    utp_mutex = erl_drv_mutex_create(const_cast<char*>("utp"));
    drv_mutex = erl_drv_mutex_create(const_cast<char*>("drv"));
    map_mutex = erl_drv_mutex_create(const_cast<char*>("utpmap"));
    return 0;
}

void
UtpDrv::Dispatcher::finish()
{
    erl_drv_mutex_destroy(map_mutex);
    erl_drv_mutex_destroy(drv_mutex);
    erl_drv_mutex_destroy(utp_mutex);
}

ErlDrvSSizeT
UtpDrv::Dispatcher::connect_start(char* buf, ErlDrvSizeT len,
                                  char** rbuf, ErlDrvSizeT rlen)
{
    int index = 0, arity, vsn, type, size;
    if (ei_decode_version(buf, &index, &vsn) != 0) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    if (ei_decode_tuple_header(buf, &index, &arity) != 0 || arity != 4) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    char addrstr[INET6_ADDRSTRLEN];
    if (ei_decode_string(buf, &index, addrstr) != 0) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    unsigned long port;
    if (ei_decode_ulong(buf, &index, &port) != 0) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    if (ei_skip_term(buf, &index) != 0) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    if (ei_get_type(buf, &index, &type, &size) != 0 || type != ERL_BINARY_EXT) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    ErlDrvBinary* from = driver_alloc_binary(size);
    long lsize;
    if (ei_decode_binary(buf, &index, from->orig_bytes, &lsize) != 0) {
        driver_free_binary(from);
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    SockAddr addr;
    socklen_t slen;
    if (!addrport_to_sockaddr(addrstr, port, addr, slen)) {
        driver_free_binary(from);
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    int udp_sock;
    int err = open_udp_socket(udp_sock);
    if (err != 0) {
        output_error_atom(erl_errno_id(err), from);
        driver_free_binary(from);
        return 0;
    }
    Port* utp_port = new Port(*this, udp_sock);
    {
        FdMap::value_type val(udp_sock, utp_port);
        MutexLocker lock(map_mutex);
        fdmap.insert(val);
    }
    ErlDrvEvent ev = reinterpret_cast<ErlDrvEvent>(udp_sock);
    driver_select(drv_port, ev, ERL_DRV_READ|ERL_DRV_USE, 1);
    utp_port->connect_to(addr, slen);
    ErlDrvPort new_port = create_port(drv_owner, utp_port);
    {
        ErlDrvTermData ext = reinterpret_cast<ErlDrvTermData>(from->orig_bytes);
        ErlDrvTermData term[] = {
            ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("ok")),
            ERL_DRV_PORT, driver_mk_port(new_port),
            ERL_DRV_EXT2TERM, ext, from->orig_size,
            ERL_DRV_TUPLE, 3,
        };
        MutexLocker lock(drv_mutex);
        driver_output_term(drv_port, term, sizeof term/sizeof *term);
        driver_free_binary(from);
    }
    return 0;
}

ErlDrvSSizeT
UtpDrv::Dispatcher::connect_validate(const char* buf, ErlDrvSizeT len,
                                     char** rbuf, ErlDrvSizeT rlen)
{
    return 0;
}

ErlDrvSSizeT
UtpDrv::Dispatcher::listen(char* buf, ErlDrvSizeT len,
                           char** rbuf, ErlDrvSizeT rlen)
{
    int index = 0, arity, vsn, type, size;
    if (ei_decode_version(buf, &index, &vsn) != 0) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    if (ei_decode_tuple_header(buf, &index, &arity) != 0 || arity != 2) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    unsigned long port;
    if (ei_decode_ulong(buf, &index, &port) != 0) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    if (ei_get_type(buf, &index, &type, &size) != 0 || type != ERL_BINARY_EXT) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    ErlDrvBinary* from = driver_alloc_binary(size);
    long lsize;
    if (ei_decode_binary(buf, &index, from->orig_bytes, &lsize) != 0) {
        driver_free_binary(from);
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    ErlDrvTermData ext = reinterpret_cast<ErlDrvTermData>(from->orig_bytes);

    int udp_sock;
    int err = open_udp_socket(udp_sock, port);
    if (err != 0) {
        ErlDrvTermData term[] = {
            ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("error")),
            ERL_DRV_ATOM, driver_mk_atom(erl_errno_id(err)),
            ERL_DRV_EXT2TERM, ext, from->orig_size,
            ERL_DRV_TUPLE, 3,
        };
        MutexLocker lock(drv_mutex);
        driver_output_term(drv_port, term, sizeof term/sizeof *term);
    } else {
        Listener* listener = new Listener(*this, udp_sock);
        ErlDrvEvent ev = reinterpret_cast<ErlDrvEvent>(udp_sock);
        driver_select(drv_port, ev, ERL_DRV_READ|ERL_DRV_USE, 1);
        ErlDrvPort new_port = create_port(drv_owner, listener, false);
        listener->set_port(new_port, drv_owner);
        ErlDrvTermData term[] = {
            ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("ok")),
            ERL_DRV_PORT, driver_mk_port(new_port),
            ERL_DRV_EXT2TERM, ext, from->orig_size,
            ERL_DRV_TUPLE, 3,
        };
        {
            MutexLocker lock(drv_mutex);
            driver_output_term(drv_port, term, sizeof term/sizeof *term);
        }
        FdMap::value_type val(udp_sock, listener);
        MutexLocker lock(map_mutex);
        fdmap.insert(val);
    }
    driver_free_binary(from);
    return 0;
}

ErlDrvSSizeT
UtpDrv::Dispatcher::close(const char* buf, ErlDrvSizeT len,
                          char** rbuf, ErlDrvSizeT rlen)
{
    return 0;
}

ErlDrvSSizeT
UtpDrv::Dispatcher::send(const char* buf, ErlDrvSizeT len,
                         char** rbuf, ErlDrvSizeT rlen)
{
    return 0;
}

ErlDrvSSizeT
UtpDrv::Dispatcher::sockname(const char* buf, ErlDrvSizeT len,
                             char** rbuf, ErlDrvSizeT rlen)
{
    return 0;
}

ErlDrvSSizeT
UtpDrv::Dispatcher::peername(const char* buf, ErlDrvSizeT len,
                             char** rbuf, ErlDrvSizeT rlen)
{
    return 0;
}

void
UtpDrv::Dispatcher::process_exit(ErlDrvMonitor* monitor)
{
}

ErlDrvPort
UtpDrv::Dispatcher::create_port(ErlDrvTermData owner, Port* p, bool do_set_port)
{
    ErlDrvData port_drv_data = reinterpret_cast<ErlDrvData>(p);
    ErlDrvPort new_port = driver_create_port(drv_port, owner,
                                             drv_name, port_drv_data);
    if (do_set_port) {
        p->set_port(new_port);
    }
    return new_port;
}

void
UtpDrv::Dispatcher::read_ready(int fd)
{
    FdMap::iterator it;
    {
        MutexLocker lock(map_mutex);
        it = fdmap.find(fd);
    }
    if (it != fdmap.end()) {
        it->second->readable();
    }
}

void
UtpDrv::Dispatcher::deselect(int fd)
{
    if (drv_port != 0 && drv_port != reinterpret_cast<ErlDrvPort>(-1)) {
        driver_select(drv_port, reinterpret_cast<ErlDrvEvent>(fd),
                      ERL_DRV_READ|ERL_DRV_USE, 0);
    }
    MutexLocker lock(map_mutex);
    fdmap.erase(fd);
}

int
UtpDrv::Dispatcher::open_udp_socket(int& udp_sock, unsigned short port)
{
    if ((udp_sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        return errno;
    }
    int flags = fcntl(udp_sock, F_GETFL);
    if (fcntl(udp_sock, F_SETFL, flags|O_NONBLOCK) < 0) {
        ::close(udp_sock);
        return errno;
    }
    SockAddr addr;
    socklen_t slen;
    addrport_to_sockaddr("0.0.0.0", port, addr, slen);
    if (bind(udp_sock, reinterpret_cast<sockaddr*>(&addr), slen) < 0) {
        ::close(udp_sock);
        return errno;
    }
    return 0;
}

void
UtpDrv::Dispatcher::output_error_atom(const char* err, const ErlDrvBinary* b)
{
    ErlDrvTermData bin_ext = reinterpret_cast<ErlDrvTermData>(b->orig_bytes);
    ErlDrvTermData term[] = {
        ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("error")),
        ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>(err)),
        ERL_DRV_EXT2TERM, bin_ext, b->orig_size,
        ERL_DRV_TUPLE, 3,
    };
    MutexLocker lock(drv_mutex);
    driver_output_term(drv_port, term, sizeof term/sizeof *term);
}

bool
UtpDrv::Dispatcher::addrport_to_sockaddr(const char* addr,
                                         unsigned short port,
                                         SockAddr& sa_arg, socklen_t& slen)
{
    memset(&sa_arg, 0, sizeof sa_arg);
    sockaddr* sa = reinterpret_cast<sockaddr*>(&sa_arg);
    addrinfo hints;
    addrinfo *ai;
    memset(&hints, 0, sizeof hints);
    hints.ai_family = PF_UNSPEC;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_NUMERICHOST;
    if (getaddrinfo(addr, 0, &hints, &ai) != 0) {
        return false;
    }
    bool result = true;
    memcpy(sa, ai->ai_addr, ai->ai_addrlen);
    slen = ai->ai_addrlen;
    if (sa->sa_family == AF_INET) {
        sockaddr_in* sa_in = reinterpret_cast<sockaddr_in*>(sa);
        sa_in->sin_port = htons(port);
    } else if (sa->sa_family == AF_INET6) {
        sockaddr_in6* sa_in6 = reinterpret_cast<sockaddr_in6*>(sa);
        sa_in6->sin6_port = htons(port);
    } else {
        result = false;
    }
    freeaddrinfo(ai);
    return result;
}

bool
UtpDrv::Dispatcher::sockaddr_to_addrport(const SockAddr& sa_arg,
                                         socklen_t slen,
                                         char* addr, size_t addrlen,
                                         unsigned short& port)
{
    const sockaddr* sa = reinterpret_cast<const sockaddr*>(&sa_arg);
    if (getnameinfo(sa, slen, addr, addrlen, 0, 0, NI_NUMERICHOST) != 0) {
        return false;
    }
    if (sa->sa_family == AF_INET) {
        const sockaddr_in* sa_in = reinterpret_cast<const sockaddr_in*>(sa);
        port = ntohs(sa_in->sin_port);
    } else if (sa->sa_family == AF_INET6) {
        const sockaddr_in6* sa_in6 = reinterpret_cast<const sockaddr_in6*>(sa);
        port = ntohs(sa_in6->sin6_port);
    } else {
        return false;
    }
    return true;
}



// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// End:
