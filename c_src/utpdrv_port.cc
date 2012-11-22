// -------------------------------------------------------------------
//
// utpdrv_port.cc: Erlang driver port for uTP
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
#include "ei.h"
#include "utpdrv_port.h"
#include "locker.h"


using namespace UtpDrv;

UtpDrv::Port::Port(Dispatcher& disp, int sock) :
    Dispatcher(reinterpret_cast<ErlDrvPort>(-1)),
    topdisp(disp), utp(0), close_ref(0), status(not_connected),
    udp_sock(sock), state(0), error_code(0),
    writable(false), connect_ref(0)
{
}

UtpDrv::Port::~Port()
{
    udp_sock = INVALID_SOCKET;
}

void
UtpDrv::Port::connect_to(const SockAddr& addr, socklen_t slen)
{
    status = connect_pending;
    if (connect_ref != 0) {
        driver_free_binary(connect_ref);
        connect_ref = 0;
    }
    const sockaddr* saddr = reinterpret_cast<const sockaddr*>(&addr);
    MutexLocker lock(utp_mutex);
    utp = UTP_Create(&Port::send_to, this, saddr, slen);
    set_callbacks();
    UTP_Connect(utp);
}

void
UtpDrv::Port::set_callbacks()
{
    UTPFunctionTable funcs = {
        &Port::utp_read,
        &Port::utp_write,
        &Port::utp_get_rb_size,
        &Port::utp_state_change,
        &Port::utp_error,
        &Port::utp_overhead,
    };
    UTP_SetCallbacks(utp, &funcs, this);
}

void
UtpDrv::Port::readable()
{
    printf("called Port::readable\r\n"); fflush(stdout);
    byte buf[4096];
    SockAddr addr;
    socklen_t salen = sizeof addr;
    int len = recvfrom(udp_sock, buf, sizeof buf, 0,
                       reinterpret_cast<sockaddr*>(&addr), &salen);
    if (len > 0) {
        MutexLocker lock(utp_mutex);
        UTP_IsIncomingUTP(&Port::utp_incoming,
                          &Port::send_to, this,
                          buf, len,
                          reinterpret_cast<sockaddr*>(&addr), salen);
    }
}

void
UtpDrv::Port::set_port(ErlDrvPort port)
{
    printf("called Port::set_port/1\r\n"); fflush(stdout);
    drv_port = port;
    set_port_control_flags(drv_port, PORT_CONTROL_FLAG_BINARY);
    pdl = driver_pdl_create(drv_port);
}

void
UtpDrv::Port::set_port(ErlDrvPort port, ErlDrvTermData owner)
{
    printf("called Port::set_port/2\r\n"); fflush(stdout);
    drv_port = port;
    drv_owner = owner;
    driver_monitor_process(drv_port, drv_owner, &owner_mon);
    set_port_control_flags(drv_port, PORT_CONTROL_FLAG_BINARY);
    pdl = driver_pdl_create(drv_port);
}

ErlDrvSSizeT
UtpDrv::Port::connect_validate(const char* buf, ErlDrvSizeT len,
                               char** rbuf, ErlDrvSizeT rlen)
{
    printf("called Port::connect_validate\r\n"); fflush(stdout);
    // assign the owner here because with this call we're now
    // assured the caller requesting the connection has been
    // linked to the port in gen_utp
    drv_owner = driver_connected(drv_port);
    driver_monitor_process(drv_port, drv_owner, &owner_mon);

    int index = 0, vsn, type, size;
    if (ei_decode_version(buf, &index, &vsn) != 0) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    if (ei_get_type(buf, &index, &type, &size) != 0 || type != ERL_BINARY_EXT) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    ErlDrvBinary* ref = driver_alloc_binary(size);
    long lsize;
    if (ei_decode_binary(buf, &index, ref->orig_bytes, &lsize) != 0) {
        driver_free_binary(ref);
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    ei_x_buff xbuf;
    ei_x_new_with_version(&xbuf);
    switch (status) {
    case connect_pending:
        ei_x_encode_atom(&xbuf, "wait");
        connect_ref = ref;
        break;

    case connect_failed:
        ei_x_encode_tuple_header(&xbuf, 2);
        ei_x_encode_atom(&xbuf, "error");
        ei_x_encode_atom(&xbuf, erl_errno_id(error_code));
        status = not_connected;
        driver_free_binary(ref);
        break;

    case connected:
        ei_x_encode_atom(&xbuf, "ok");
        driver_free_binary(ref);
        break;

    default:
        ei_x_encode_tuple_header(&xbuf, 2);
        ei_x_encode_atom(&xbuf, "error");
        {
            char err[64];
            sprintf(err, "utpdrv illegal connect state: %d", status);
            ei_x_encode_string(&xbuf, err);
        }
        driver_free_binary(ref);
        break;
    }
    if (xbuf.index <= int(rlen-1)) {
        memcpy(*rbuf, xbuf.buff, xbuf.index+1);
    } else {
        ErlDrvBinary* bin = driver_alloc_binary(xbuf.index+1);
        memcpy(bin->orig_bytes, xbuf.buff, xbuf.index+1);
        *rbuf = bin->orig_bytes;
    }
    return xbuf.index+1;
}

ErlDrvSSizeT
UtpDrv::Port::close(const char* buf, ErlDrvSizeT len,
                    char** rbuf, ErlDrvSizeT rlen)
{
    printf("called Port::close\r\n"); fflush(stdout);
    int index = 0, vsn, type, size;
    if (ei_decode_version(buf, &index, &vsn) != 0) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    if (ei_get_type(buf, &index, &type, &size) != 0 || type != ERL_BINARY_EXT) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    close_ref = driver_alloc_binary(size);
    long lsize;
    if (ei_decode_binary(buf, &index, close_ref->orig_bytes, &lsize) != 0) {
        driver_free_binary(close_ref);
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    status = closing;
    const char* retval = "ok";
    if (utp != 0) {
        // Switch the owner to the caller in case the socket is closed
        // by a process other than the one it's connected to. This way
        // the message sent by do_state_change will go to the right place.
        drv_owner = driver_caller(drv_port);
        retval = "wait";
        MutexLocker lock(utp_mutex);
        UTP_Close(utp);
    }
    index = 0;
    ei_encode_version(*rbuf, &index);
    ei_encode_atom(*rbuf, &index, retval);
    return index+1;
}

ErlDrvSSizeT
UtpDrv::Port::send(const char* buf, ErlDrvSizeT len,
                   char** rbuf, ErlDrvSizeT rlen)
{
    printf("called Port::send\r\n"); fflush(stdout);
    if (status != connected || utp == 0) {
        return enotconn(rbuf);
    }

    if (len != 0) {
        {
            PdlLocker pdl_lock(pdl);
            driver_enq(drv_port, const_cast<char*>(buf), len);
        }
        {
            MutexLocker lock(utp_mutex);
            writable = UTP_Write(utp, len);
        }
    }
    int index = 0;
    ei_encode_version(*rbuf, &index);
    ei_encode_atom(*rbuf, &index, "ok");
    return index+1;
}

ErlDrvSSizeT
UtpDrv::Port::sockname(const char* buf, ErlDrvSizeT len,
                       char** rbuf, ErlDrvSizeT rlen)
{
    printf("called Port::sockname\r\n"); fflush(stdout);
    SockAddr addr;
    socklen_t slen = sizeof addr;
    int index = 0, result = getsockname(udp_sock,
                                        reinterpret_cast<sockaddr*>(&addr),
                                        &slen);
    if (result < 0) {
        ei_encode_version(*rbuf, &index);
        ei_encode_tuple_header(*rbuf, &index, 2);
        ei_encode_atom(*rbuf, &index, "error");
        ei_encode_atom(*rbuf, &index, erl_errno_id(errno));
        ++index;
    } else {
        ei_x_buff xbuf;
        index = encode_addrport(xbuf, addr, slen, rbuf, rlen);
    }
    return index;
}

ErlDrvSSizeT
UtpDrv::Port::peername(const char* buf, ErlDrvSizeT len,
                       char** rbuf, ErlDrvSizeT rlen)
{
    printf("called Port::peername\r\n"); fflush(stdout);
    if (status != connected || utp == 0) {
        return enotconn(rbuf);
    }
    SockAddr addr;
    socklen_t slen = sizeof addr;
    {
        MutexLocker lock(utp_mutex);
        UTP_GetPeerName(utp, reinterpret_cast<sockaddr*>(&addr), &slen);
    }
    ei_x_buff xbuf;
    return encode_addrport(xbuf, addr, slen, rbuf, rlen);
}

void
UtpDrv::Port::process_exit(ErlDrvMonitor* monitor)
{
    printf("called Port::process_exit\r\n"); fflush(stdout);
    if (udp_sock != INVALID_SOCKET) {
        driver_select(drv_port, reinterpret_cast<ErlDrvEvent>(udp_sock),
                      ERL_DRV_READ|ERL_DRV_USE, 0);
    }
    if (drv_port != reinterpret_cast<ErlDrvPort>(-1)) {
        driver_failure_eof(drv_port);
    }
    if (utp != 0) {
        MutexLocker lock(utp_mutex);
        UTP_Close(utp);
    }
}

void
UtpDrv::Port::stop()
{
    printf("called Port::stop\r\n"); fflush(stdout);
    if (drv_port != reinterpret_cast<ErlDrvPort>(-1)) {
        driver_demonitor_process(drv_port, &owner_mon);
    }
}

void
UtpDrv::Port::do_send_to(const byte* p, size_t len,
                         const sockaddr* to, socklen_t slen)
{
    printf("called Port::do_send_to for size %ld\r\n", len); fflush(stdout);
    int count = sendto(udp_sock, p, len, 0, to, slen);
    if (count < 0) {
        // handle error
    }
}

void
UtpDrv::Port::do_read(const byte* bytes, size_t count)
{
    printf("called Port::do_read for size %ld\r\n", count); fflush(stdout);
    if (count == 0) return;
    char* buf0 = const_cast<char*>(reinterpret_cast<const char*>(bytes));
    ErlDrvTermData data = reinterpret_cast<ErlDrvTermData>(buf0);
    ErlDrvTermData term[] = {
        ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("utp")),
        ERL_DRV_PORT, driver_mk_port(drv_port),
        ERL_DRV_BUF2BINARY, data, count,
        ERL_DRV_TUPLE, 3,
    };
    MutexLocker lock(drv_mutex);
    driver_output_term(drv_port, term, sizeof term/sizeof *term);
}

void
UtpDrv::Port::do_write(byte* bytes, size_t count)
{
    printf("called Port::do_write for size %ld\r\n", count); fflush(stdout);
    if (count == 0) return;
    PdlLocker pdl_lock(pdl);
    if (driver_sizeq(drv_port) < count) {
        // error
    }
    int vlen, index = 0, i = 0;
    SysIOVec* iovec = driver_peekq(drv_port, &vlen);
    for (size_t needed = count; i < vlen && needed != 0; ++i) {
        unsigned long iov_len = iovec[i].iov_len;
        size_t to_copy = (iov_len > needed) ? needed : iov_len;
        memcpy(bytes+index, iovec[i].iov_base, to_copy);
        index += to_copy;
        needed -= to_copy;
    }
    driver_deq(drv_port, count);
}

size_t
UtpDrv::Port::do_get_rb_size()
{
    return 0;
}

void
UtpDrv::Port::do_state_change(int s)
{
    printf("called Port::do_state_change for state %d\r\n", s); fflush(stdout);
    if (state == s) return;
    state = s;
    switch (state) {
    case UTP_STATE_EOF:
        status = not_connected;
        break;

    case UTP_STATE_WRITABLE:
        writable = true;
        break;

    case UTP_STATE_CONNECT:
        if (status == connect_pending && connect_ref != 0) {
            ErlDrvTermData ext = reinterpret_cast<ErlDrvTermData>(
                connect_ref->orig_bytes);
            ErlDrvTermData term[] = {
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("ok")),
                ERL_DRV_EXT2TERM, ext, connect_ref->orig_size,
                ERL_DRV_TUPLE, 2,
            };
            MutexLocker lock(drv_mutex);
            driver_output_term(drv_port, term, sizeof term/sizeof *term);
            driver_free_binary(connect_ref);
            connect_ref = 0;
        }
        status = connected;
        break;

    case UTP_STATE_DESTROYING:
        if (udp_sock != INVALID_SOCKET) {
            topdisp.deselect(udp_sock);
        }
        if (status == closing) {
            ErlDrvTermData ext = reinterpret_cast<ErlDrvTermData>(
                close_ref->orig_bytes);
            ErlDrvTermData term[] = {
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("ok")),
                ERL_DRV_EXT2TERM, ext, close_ref->orig_size,
                ERL_DRV_TUPLE, 2,
            };
            MutexLocker lock(drv_mutex);
            driver_output_term(drv_port, term, sizeof term/sizeof *term);
            driver_free_binary(close_ref);
            close_ref = 0;
        }
        break;
    }
}

void
UtpDrv::Port::do_error(int errcode)
{
    printf("called Port::do_error for error %d\r\n", errcode); fflush(stdout);
    error_code = errcode;
    switch (status) {
    case connect_pending:
        status = connect_failed;
        if (connect_ref != 0) {
            ErlDrvTermData ext = reinterpret_cast<ErlDrvTermData>(connect_ref->orig_bytes);
            ErlDrvTermData term[] = {
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("error")),
                ERL_DRV_ATOM, driver_mk_atom(erl_errno_id(error_code)),
                ERL_DRV_EXT2TERM, ext, connect_ref->orig_size,
                ERL_DRV_TUPLE, 3,
            };
            MutexLocker lock(drv_mutex);
            driver_output_term(drv_port, term, sizeof term/sizeof *term);
            driver_free_binary(connect_ref);
        }
        break;
    default:
        break;
    }
}

void
UtpDrv::Port::do_overhead(bool send, size_t count, int type)
{
}

void
UtpDrv::Port::do_incoming(UTPSocket* utp)
{
    printf("called Port::do_incoming"); fflush(stdout);
}

int
UtpDrv::Port::encode_addrport(ei_x_buff& xbuf, const SockAddr&addr,
                              socklen_t slen, char** rbuf, ErlDrvSizeT rlen)
{
    char addrstr[INET6_ADDRSTRLEN];
    unsigned short port;
    sockaddr_to_addrport(addr, slen, addrstr, sizeof addrstr, port);
    ei_x_new_with_version(&xbuf);
    ei_x_encode_tuple_header(&xbuf, 2);
    ei_x_encode_atom(&xbuf, "ok");
    ei_x_encode_tuple_header(&xbuf, 2);
    ei_x_encode_string(&xbuf, addrstr);
    ei_x_encode_ulong(&xbuf, port);
    int index = xbuf.index+1;
    if (index <= int(rlen)) {
        memcpy(*rbuf, xbuf.buff, index);
    } else {
        ErlDrvBinary* bin = driver_alloc_binary(index);
        memcpy(bin->orig_bytes, xbuf.buff, index);
        *rbuf = bin->orig_bytes;
    }
    return index;
}

int
UtpDrv::Port::enotconn(char** rbuf)
{
    int index = 0;
    ei_encode_version(*rbuf, &index);
    ei_encode_tuple_header(*rbuf, &index, 2);
    ei_encode_atom(*rbuf, &index, "error");
    ei_encode_atom(*rbuf, &index, erl_errno_id(ENOTCONN));
    return index+1;
}
