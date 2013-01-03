// -------------------------------------------------------------------
//
// utp_port.cc: base class for created uTP ports
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

#include "utp_port.h"
#include "locker.h"
#include "globals.h"
#include "main_port.h"
#include "utils.h"


using namespace UtpDrv;

UtpDrv::UtpPort::UtpPort(int sock, DataDelivery del, long send_tm) :
    SocketHandler(sock),
    send_tmout(send_tm), pdl(0), caller(driver_term_nil), utp(0),
    status(not_connected), data_delivery(del), state(0),
    error_code(0), writable(false), mon_valid(false)
{
    write_q_mutex = erl_drv_mutex_create(const_cast<char*>("write_q_mutex"));
}

UtpDrv::UtpPort::~UtpPort()
{
    erl_drv_mutex_destroy(write_q_mutex);
}

void
UtpDrv::UtpPort::process_exit(ErlDrvMonitor* monitor)
{
    UTPDRV_TRACE("UtpPort::process_exit\r\n");
    MainPort::del_monitor(port, *monitor);
    mon_valid = false;
    driver_failure_eof(port);
}

void
UtpDrv::UtpPort::set_port(ErlDrvPort p)
{
    UTPDRV_TRACE("UtpPort::set_port\r\n");
    Handler::set_port(p);
    pdl = driver_pdl_create(port);
    if (pdl == 0) {
        driver_failure_atom(port, const_cast<char*>("port_data_lock_failed"));
    }
}

void
UtpDrv::UtpPort::input_ready()
{
    // TODO: when recv buffer sizes can be varied, the following buffer will
    // need to come from a pool
    byte buf[8192];
    SockAddr addr;
    int len = recvfrom(udp_sock, buf, sizeof buf, 0, addr, &addr.slen);
    if (len > 0) {
        MutexLocker lock(utp_mutex);
        UTP_IsIncomingUTP(&UtpPort::utp_incoming,
                          &UtpPort::send_to, this,
                          buf, len, addr, addr.slen);
    }
}

void
UtpDrv::UtpPort::demonitor()
{
    UTPDRV_TRACE("UtpPort::demonitor\r\n");
    MainPort::del_monitor(port, mon);
    mon_valid = false;
}

// NB: this function must be called with the utp_mutex locked.
void
UtpDrv::UtpPort::set_utp_callbacks(UTPSocket* utp)
{
    UTPDRV_TRACE("UtpPort::set_utp_callbacks\r\n");
    UTPFunctionTable funcs = {
        &UtpPort::utp_read,
        &UtpPort::utp_write,
        &UtpPort::utp_get_rb_size,
        &UtpPort::utp_state_change,
        &UtpPort::utp_error,
        &UtpPort::utp_overhead,
    };
    UTP_SetCallbacks(utp, &funcs, this);
}

ErlDrvSSizeT
UtpDrv::UtpPort::peername(const char* buf, ErlDrvSizeT len,
                          char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACE("UtpPort::peername\r\n");
    if (status != connected || utp == 0) {
        return encode_error(rbuf, rlen, ENOTCONN);
    }
    SockAddr addr;
    {
        MutexLocker lock(utp_mutex);
        UTP_GetPeerName(utp, addr, &addr.slen);
    }
    return addr.encode(rbuf, rlen);
}

void
UtpDrv::UtpPort::outputv(ErlIOVec& ev)
{
    UTPDRV_TRACE("UtpPort::outputv\r\n");
    if (status != connected || utp == 0) {
        send_not_connected(port);
        return;
    }

    ErlDrvTermData caller = driver_caller(port);
    ErlDrvTermData utp_reply = driver_mk_atom(const_cast<char*>("utp_reply"));
    if (writable) {
        if (ev.size > 0) {
            MutexLocker lock(write_q_mutex);
            for (int i = 0; i < ev.vsize; ++i) {
                ErlDrvBinary* bin = 0;
                if (ev.binv[i] != 0) {
                    bin = ev.binv[i];
                    driver_binary_inc_refc(bin);
                } else if (ev.iov[i].iov_len > 0) {
                    const SysIOVec& vec = ev.iov[i];
                    bin = driver_alloc_binary(vec.iov_len);
                    memcpy(bin->orig_bytes, vec.iov_base, vec.iov_len);
                }
                if (bin != 0) {
                    write_queue.push_back(bin);
                }
            }
        }
        {
            MutexLocker lock(utp_mutex);
            writable = UTP_Write(utp, write_queue.size());
        }
        ErlDrvTermData term[] = {
            ERL_DRV_ATOM, utp_reply,
            ERL_DRV_PORT, driver_mk_port(port),
            ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("ok")),
            ERL_DRV_TUPLE, 3,
        };
        driver_send_term(port, caller, term, sizeof term/sizeof *term);
    } else {
        if (send_tmout == 0) {
            ErlDrvTermData term[] = {
                ERL_DRV_ATOM, utp_reply,
                ERL_DRV_PORT, driver_mk_port(port),
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("error")),
                ERL_DRV_ATOM, driver_mk_atom(erl_errno_id(ETIMEDOUT)),
                ERL_DRV_TUPLE, 2,
                ERL_DRV_TUPLE, 3,
            };
            driver_send_term(port, caller, term, sizeof term/sizeof *term);
        } else {
            {
                MutexLocker lock(utp_mutex);
                waiting_writers.push_back(caller);
            }
            ErlDrvTermData term[12] = {
                ERL_DRV_ATOM, utp_reply,
                ERL_DRV_PORT, driver_mk_port(port),
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("wait")),
            };
            size_t size = 6;
            if (send_tmout == -1) {
                term[size++] = ERL_DRV_TUPLE;
                term[size++] = 3;
            } else {
                term[size++] = ERL_DRV_UINT;
                term[size++] = send_tmout;
                term[size++] = ERL_DRV_TUPLE;
                term[size++] = 2;
                term[size++] = ERL_DRV_TUPLE;
                term[size++] = 3;
            }
            driver_send_term(port, caller, term, size);
        }
    }
}

void
UtpDrv::UtpPort::stop()
{
    UTPDRV_TRACE("Client::stop\r\n");
    MainPort::stop_input(udp_sock);
    if (utp == 0 && status == destroying) {
        delete this;
    } else {
        if (utp != 0) {
            MutexLocker lock(utp_mutex);
            close_utp();
        }
        status = stopped;
    }
}

ErlDrvSSizeT
UtpDrv::UtpPort::close(const char* buf, ErlDrvSizeT len,
                       char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACE("UtpPort::close\r\n");
    const char* retval = "ok";
    if (status != closing && status != destroying) {
        MutexLocker lock(utp_mutex);
        status = closing;
        if (mon_valid) {
            MainPort::del_monitor(port, mon);
            mon_valid = false;
        }
        if (utp != 0) {
            try {
                EiDecoder decoder(buf, len);
                int type, size;
                decoder.type(type, size);
                if (type != ERL_BINARY_EXT) {
                    return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
                }
                caller_ref.decode(decoder, size);
            } catch (const EiError&) {
                return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
            }
            caller = driver_caller(port);
            retval = "wait";
            close_utp();
        }
    }
    EiEncoder encoder;
    encoder.atom(retval);
    ErlDrvBinary** binptr = reinterpret_cast<ErlDrvBinary**>(rbuf);
    return encoder.copy_to_binary(binptr, rlen);
}

ErlDrvSSizeT
UtpDrv::UtpPort::setopts(const char* buf, ErlDrvSizeT len,
                         char** rbuf, ErlDrvSizeT rlen)
{
    // TODO: implement options
    return 0;
}

ErlDrvSSizeT
UtpDrv::UtpPort::cancel_send()
{
    ErlDrvTermData caller = driver_caller(port);
    MutexLocker lock(utp_mutex);
    WaitingWriters::iterator it = waiting_writers.begin();
    while (it != waiting_writers.end()) {
        if (*it == caller) {
            waiting_writers.erase(it);
            break;
        }
    }
    return 0;
}

void
UtpDrv::UtpPort::close_utp()
{
    if (utp != 0 && status != destroying) {
        status = closing;
        UTP_Close(utp);
        utp = 0;
    }
}

void
UtpDrv::UtpPort::do_send_to(const byte* p, size_t len,
                            const sockaddr* to, socklen_t slen)
{
    UTPDRV_TRACE("UtpPort::do_send_to\r\n");
    if (udp_sock != INVALID_SOCKET) {
        int index = 0;
        for (;;) {
            ssize_t count = sendto(udp_sock, p+index, len-index, 0, to, slen);
            if (count == ssize_t(len-index)) {
                break;
            } else if (count < 0 && count != EINTR &&
                       count != EAGAIN && count != EWOULDBLOCK) {
                close_utp();
                break;
            } else {
                index += count;
            }
        }
    }
}

void
UtpDrv::UtpPort::do_read(const byte* bytes, size_t count)
{
    UTPDRV_TRACE("UtpPort::do_read\r\n");
    if (count == 0) return;
    char* buf = const_cast<char*>(reinterpret_cast<const char*>(bytes));
    ErlDrvTermData data = reinterpret_cast<ErlDrvTermData>(buf);
    ErlDrvTermData term[] = {
        ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("utp")),
        ERL_DRV_PORT, driver_mk_port(port),
        ERL_DRV_BUF2BINARY, data, count,
        ERL_DRV_TUPLE, 3,
    };
    if (data_delivery == DATA_LIST) {
        term[4] = ERL_DRV_STRING;
    }
    MutexLocker lock(drv_mutex);
    driver_output_term(port, term, sizeof term/sizeof *term);
}

void
UtpDrv::UtpPort::do_write(byte* bytes, size_t count)
{
    UTPDRV_TRACE("UtpPort::do_write\r\n");
    if (count == 0) return;
    MutexLocker lock(write_q_mutex);
    write_queue.pop_bytes(bytes, count);
}

size_t
UtpDrv::UtpPort::do_get_rb_size()
{
    UTPDRV_TRACE("UtpPort::do_get_rb_size\r\n");
    return 0;
}

void
UtpDrv::UtpPort::do_state_change(int s)
{
    UTPDRV_TRACE("UtpPort::do_state_change\r\n");
    state = s;
    switch (state) {
    case UTP_STATE_EOF:
        if (status == connected) {
            status = closing;
        } else {
            status = not_connected;
        }
        writable = false;
        break;

    case UTP_STATE_WRITABLE:
        writable = true;
        {
            size_t sz = write_queue.size();
            if (sz > 0) {
                writable = UTP_Write(utp, sz);
            }
        }
        if (writable) {
            ErlDrvTermData term[] = {
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("utp_reply")),
                ERL_DRV_PORT, driver_mk_port(port),
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("retry")),
                ERL_DRV_TUPLE, 3,
            };
            WaitingWriters::iterator it = waiting_writers.begin();
            while (it != waiting_writers.end()) {
                driver_send_term(port, *it++, term, sizeof term/sizeof *term);
            }
            waiting_writers.clear();
        }
        break;

    case UTP_STATE_CONNECT:
        if (status == connect_pending && caller_ref) {
            ErlDrvTermData term[] = {
                ERL_DRV_EXT2TERM, caller_ref, caller_ref.size(),
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("ok")),
                ERL_DRV_TUPLE, 2,
            };
            MutexLocker lock(drv_mutex);
            driver_output_term(port, term, sizeof term/sizeof *term);
        }
        status = connected;
        writable = true;
        break;

    case UTP_STATE_DESTROYING:
        MainPort::stop_input(udp_sock);
        if (status == closing) {
            ErlDrvTermData term[] = {
                ERL_DRV_EXT2TERM, caller_ref, caller_ref.size(),
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("ok")),
                ERL_DRV_TUPLE, 2,
            };
            if (caller != driver_term_nil) {
                driver_send_term(port, caller, term, sizeof term/sizeof *term);
            } else {
                MutexLocker lock(drv_mutex);
                driver_output_term(port, term, sizeof term/sizeof *term);
            }
            caller = driver_term_nil;
            writable = false;
            status = destroying;
        } else if (status == stopped) {
            delete this;
        }
        break;
    }
}

void
UtpDrv::UtpPort::do_error(int errcode)
{
    UTPDRV_TRACE("UtpPort::do_error\r\n");
    error_code = errcode;
    switch (status) {
    case connect_pending:
        status = connect_failed;
        if (caller_ref) {
            ErlDrvTermData term[] = {
                ERL_DRV_EXT2TERM, caller_ref, caller_ref.size(),
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("error")),
                ERL_DRV_ATOM, driver_mk_atom(erl_errno_id(error_code)),
                ERL_DRV_TUPLE, 2,
                ERL_DRV_TUPLE, 2,
            };
            MutexLocker lock(drv_mutex);
            driver_output_term(port, term, sizeof term/sizeof *term);
        }
        break;
    default:
        break;
    }
}

void
UtpDrv::UtpPort::do_overhead(bool send, size_t count, int type)
{
    // do nothing
}

void
UtpDrv::UtpPort::send_to(void* data, const byte* p, size_t len,
                         const sockaddr* to, socklen_t slen)
{
    (static_cast<UtpPort*>(data))->do_send_to(p, len, to, slen);
}

void
UtpDrv::UtpPort::utp_read(void* data, const byte* bytes, size_t count)
{
    (static_cast<UtpPort*>(data))->do_read(bytes, count);
}

void
UtpDrv::UtpPort::utp_write(void* data, byte* bytes, size_t count)
{
    (static_cast<UtpPort*>(data))->do_write(bytes, count);
}

size_t
UtpDrv::UtpPort::utp_get_rb_size(void* data)
{
    return (static_cast<UtpPort*>(data))->do_get_rb_size();
}

void
UtpDrv::UtpPort::utp_state_change(void* data, int state)
{
    (static_cast<UtpPort*>(data))->do_state_change(state);
}

void
UtpDrv::UtpPort::utp_error(void* data, int errcode)
{
    (static_cast<UtpPort*>(data))->do_error(errcode);
}

void
UtpDrv::UtpPort::utp_overhead(void* data, bool send, size_t count, int type)
{
    (static_cast<UtpPort*>(data))->do_overhead(send, count, type);
}

void
UtpDrv::UtpPort::utp_incoming(void* data, UTPSocket* utp)
{
    (static_cast<UtpPort*>(data))->do_incoming(utp);
}
