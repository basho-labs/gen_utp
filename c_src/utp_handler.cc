// -------------------------------------------------------------------
//
// utp_handler.cc: base class for uTP port handlers
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

#include <string>
#include "utp_handler.h"
#include "locker.h"
#include "globals.h"
#include "main_handler.h"
#include "utils.h"


using namespace UtpDrv;

UtpDrv::UtpHandler::UtpHandler(int sock, const SockOpts& so) :
    SocketHandler(sock, so),
    caller(driver_term_nil), utp(0), recv_len(0), status(not_connected), state(0),
    error_code(0), writable(false), sender_waiting(false), receiver_waiting(false)
{
    write_q_mutex = erl_drv_mutex_create(const_cast<char*>("write_q_mutex"));
}

UtpDrv::UtpHandler::~UtpHandler()
{
    erl_drv_mutex_destroy(write_q_mutex);
}

void
UtpDrv::UtpHandler::input_ready()
{
    // TODO: when recv buffer sizes can be varied, the following buffer will
    // need to come from a pool
    byte buf[8192];
    SockAddr addr;
    int len = recvfrom(udp_sock, buf, sizeof buf, 0, addr, &addr.slen);
    if (len > 0) {
        MutexLocker lock(utp_mutex);
        UTP_IsIncomingUTP(&UtpHandler::utp_incoming,
                          &UtpHandler::send_to, this,
                          buf, len, addr, addr.slen);
    }
}

void
UtpDrv::UtpHandler::process_exited(const ErlDrvMonitor*, ErlDrvTermData proc)
{
    ErlDrvTermData connected = driver_connected(port);
    if (proc == connected) {
        driver_failure_eof(port);
    }
}

void
UtpDrv::UtpHandler::set_utp_callbacks()
{
    UTPDRV_TRACER << "UtpHandler::set_utp_callbacks " << this << UTPDRV_TRACE_ENDL;
    if (utp != 0) {
        UTPFunctionTable funcs = {
            &UtpHandler::utp_read,
            &UtpHandler::utp_write,
            &UtpHandler::utp_get_rb_size,
            &UtpHandler::utp_state_change,
            &UtpHandler::utp_error,
            &UtpHandler::utp_overhead,
        };
        UTP_SetCallbacks(utp, &funcs, this);
    }
}

ErlDrvSSizeT
UtpDrv::UtpHandler::control(unsigned command, const char* buf, ErlDrvSizeT len,
                            char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "UtpHandler::control " << this << UTPDRV_TRACE_ENDL;
    switch (command) {
    case UTP_CANCEL_SEND:
        return cancel_send();
    case UTP_RECV:
        return recv(buf, len, rbuf, rlen);
    case UTP_CANCEL_RECV:
        return cancel_recv();
    }
    return SocketHandler::control(command, buf, len, rbuf, rlen);
}

ErlDrvSSizeT
UtpDrv::UtpHandler::peername(const char* buf, ErlDrvSizeT len,
                             char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "UtpHandler::peername " << this << UTPDRV_TRACE_ENDL;
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
UtpDrv::UtpHandler::outputv(ErlIOVec& ev)
{
    UTPDRV_TRACER << "UtpHandler::outputv " << this << UTPDRV_TRACE_ENDL;
    if (status != connected || utp == 0) {
        send_not_connected(port);
        return;
    }

    ErlDrvTermData local_caller, utp_reply;
    local_caller = driver_caller(port);
    utp_reply = driver_mk_atom(const_cast<char*>("utp_reply"));
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
        driver_send_term(port, local_caller, term, sizeof term/sizeof *term);
    } else {
        if (sockopts.send_tmout == 0) {
            ErlDrvTermData term[] = {
                ERL_DRV_ATOM, utp_reply,
                ERL_DRV_PORT, driver_mk_port(port),
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("error")),
                ERL_DRV_ATOM, driver_mk_atom(erl_errno_id(ETIMEDOUT)),
                ERL_DRV_TUPLE, 2,
                ERL_DRV_TUPLE, 3,
            };
            driver_send_term(port, local_caller, term, sizeof term/sizeof *term);
        } else {
            {
                MutexLocker lock(utp_mutex);
                caller = local_caller;
                sender_waiting = true;
            }
            ErlDrvTermData term[12] = {
                ERL_DRV_ATOM, utp_reply,
                ERL_DRV_PORT, driver_mk_port(port),
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("wait")),
            };
            size_t size = 6;
            if (sockopts.send_tmout == -1) {
                term[size++] = ERL_DRV_TUPLE;
                term[size++] = 3;
            } else {
                term[size++] = ERL_DRV_UINT;
                term[size++] = sockopts.send_tmout;
                term[size++] = ERL_DRV_TUPLE;
                term[size++] = 2;
                term[size++] = ERL_DRV_TUPLE;
                term[size++] = 3;
            }
            driver_send_term(port, local_caller, term, size);
        }
    }
}

void
UtpDrv::UtpHandler::stop()
{
    UTPDRV_TRACER << "UtpHandler::stop " << this << UTPDRV_TRACE_ENDL;
    if (utp != 0) {
        MutexLocker lock(utp_mutex);
        close_utp();
    }
    {
        PdlLocker pdl_lock(pdl);
        ErlDrvSizeT qsize = driver_sizeq(port);
        if (qsize != 0) {
            driver_deq(port, qsize);
        }
    }
    if (status == destroying) {
        delete this;
    } else {
        status = stopped;
    }
}

ErlDrvSSizeT
UtpDrv::UtpHandler::close(const char* buf, ErlDrvSizeT len,
                          char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "UtpHandler::close " << this << UTPDRV_TRACE_ENDL;
    const char* retval = "ok";
    if (status != closing && status != destroying) {
        MutexLocker lock(utp_mutex);
        status = closing;
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
UtpDrv::UtpHandler::recv(const char* buf, ErlDrvSizeT len,
                         char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "UtpHandler::recv " << this << UTPDRV_TRACE_ENDL;
    if (sockopts.active != ACTIVE_FALSE) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }
    Binary ref;
    unsigned long length;
    try {
        EiDecoder decoder(buf, len);
        int arity, type, size;
        decoder.tuple_header(arity);
        if (arity != 2) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        decoder.ulong(length);
        decoder.type(type, size);
        if (type != ERL_BINARY_EXT) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        ref.decode(decoder, size);
    } catch (const EiError&) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    EiEncoder encoder;
    PdlLocker pdl_lock(pdl);
    ErlDrvSizeT qsize = driver_sizeq(port);
    bool need_wait = false;
    if (qsize > 0) {
        if (length == 0) {
            length = qsize;
        }
        if (qsize >= length) {
            encoder.tuple_header(2).atom("ok");
            int vlen;
            SysIOVec* vec = driver_peekq(port, &vlen);
            size_t total = 0;
            ustring buf;
            buf.reserve(length+1);
            for (int i = 0; i < vlen; ++i) {
                char* p = vec[i].iov_base;
                unsigned char* uc = reinterpret_cast<unsigned char*>(p);
                if (total + vec[i].iov_len <= length) {
                    buf.append(uc, vec[i].iov_len);
                    total += vec[i].iov_len;
                } else {
                    buf.append(uc, length - total);
                    break;
                }
            }
            driver_deq(port, length);
            if (sockopts.delivery_mode == DATA_LIST) {
                encoder.string(reinterpret_cast<const char*>(buf.c_str()));
            } else {
                encoder.binary(buf.data(), total);
            }
        } else {
            need_wait = true;
        }
    } else {
        need_wait = true;
    }
    if (need_wait) {
        encoder.atom("wait");
        MutexLocker lock(utp_mutex);
        caller_ref.swap(ref);
        caller = driver_caller(port);
        recv_len = length;
        receiver_waiting = true;
    }
    ErlDrvBinary** binptr = reinterpret_cast<ErlDrvBinary**>(rbuf);
    return encoder.copy_to_binary(binptr, rlen);
}

ErlDrvSSizeT
UtpDrv::UtpHandler::cancel_send()
{
    UTPDRV_TRACER << "UtpHandler::cancel_send " << this << UTPDRV_TRACE_ENDL;
    MutexLocker lock(utp_mutex);
    sender_waiting = false;
    caller = driver_term_nil;
    return 0;
}

ErlDrvSSizeT
UtpDrv::UtpHandler::cancel_recv()
{
    UTPDRV_TRACER << "UtpHandler::cancel_recv " << this << UTPDRV_TRACE_ENDL;
    MutexLocker lock(utp_mutex);
    receiver_waiting = false;
    recv_len = 0;
    caller_ref.reset();
    caller = 0;
    return 0;
}

void
UtpDrv::UtpHandler::close_utp()
{
    UTPDRV_TRACER << "UtpHandler::close_utp " << this << UTPDRV_TRACE_ENDL;
    if (utp != 0 && status != destroying) {
        status = closing;
        UTP_Close(utp);
        utp = 0;
    }
}

void
UtpDrv::UtpHandler::do_send_to(const byte* p, size_t len,
                               const sockaddr* to, socklen_t slen)
{
    UTPDRV_TRACER << "UtpHandler::do_send_to " << this << UTPDRV_TRACE_ENDL;
    if (udp_sock != INVALID_SOCKET) {
        int index = 0;
        for (;;) {
            ssize_t count = sendto(udp_sock, p+index, len-index, 0, to, slen);
            if (count == ssize_t(len-index)) {
                break;
            } else if (count < 0 && errno != EINTR &&
                       errno != EAGAIN && errno != EWOULDBLOCK) {
                do_error(errno);
                break;
            } else {
                index += count;
            }
        }
    }
}

void
UtpDrv::UtpHandler::do_read(const byte* bytes, size_t count)
{
    UTPDRV_TRACER << "UtpHandler::do_read " << this << UTPDRV_TRACE_ENDL;
    if (count != 0) {
        ErlDrvSizeT qsize;
        char* buf = const_cast<char*>(reinterpret_cast<const char*>(bytes));
        if (sockopts.active == ACTIVE_FALSE) {
            {
                PdlLocker pdl_lock(pdl);
                driver_enq(port, buf, count);
                qsize = driver_sizeq(port);
            }
            if (receiver_waiting &&  qsize >= recv_len) {
                Receiver rcvr(false, caller, caller_ref);
                qsize = send_read_buffer(recv_len, rcvr);
                cancel_recv();
            }
        } else {
            ustring data(reinterpret_cast<unsigned char*>(buf), count);
            Receiver rcvr;
            qsize = send_read_buffer(0, rcvr, &data);
        }
        if (qsize == 0) {
            UTP_RBDrained(utp);
        }
    }
}

void
UtpDrv::UtpHandler::do_write(byte* bytes, size_t count)
{
    UTPDRV_TRACER << "UtpHandler::do_write " << this << UTPDRV_TRACE_ENDL;
    if (count == 0) return;
    MutexLocker lock(write_q_mutex);
    write_queue.pop_bytes(bytes, count);
}

size_t
UtpDrv::UtpHandler::do_get_rb_size()
{
    UTPDRV_TRACER << "UtpHandler::do_get_rb_size " << this << UTPDRV_TRACE_ENDL;
    if (status == connected) {
        PdlLocker pdl_lock(pdl);
        return driver_sizeq(port);
    } else {
        return 0;
    }
}

void
UtpDrv::UtpHandler::do_state_change(int s)
{
    UTPDRV_TRACER << "UtpHandler::do_state_change " << this << ": "
                  << "status " << status << ", current state: " << state
                  << ", new state: " << s << UTPDRV_TRACE_ENDL;
    state = s;
    switch (state) {
    case UTP_STATE_EOF:
        if (status != stopped) {
            close_utp();
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
        if (writable && sender_waiting) {
            ErlDrvTermData term[] = {
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("utp_reply")),
                ERL_DRV_PORT, driver_mk_port(port),
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("retry")),
                ERL_DRV_TUPLE, 3,
            };
            driver_send_term(port, caller, term, sizeof term/sizeof *term);
            sender_waiting = false;
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
        MainHandler::stop_input(udp_sock);
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
UtpDrv::UtpHandler::do_error(int errcode)
{
    UTPDRV_TRACER << "UtpHandler::do_error " << this << ": "
                  << "status " << status << ", error code " << errcode
                  << UTPDRV_TRACE_ENDL;
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
    case connected:
        if (errcode == ECONNRESET) {
            close_utp();
        }
        break;
    default:
        break;
    }
}

void
UtpDrv::UtpHandler::do_overhead(bool send, size_t count, int type)
{
    // do nothing
}

void
UtpDrv::UtpHandler::send_to(void* data, const byte* p, size_t len,
                            const sockaddr* to, socklen_t slen)
{
    (static_cast<UtpHandler*>(data))->do_send_to(p, len, to, slen);
}

void
UtpDrv::UtpHandler::utp_read(void* data, const byte* bytes, size_t count)
{
    (static_cast<UtpHandler*>(data))->do_read(bytes, count);
}

void
UtpDrv::UtpHandler::utp_write(void* data, byte* bytes, size_t count)
{
    (static_cast<UtpHandler*>(data))->do_write(bytes, count);
}

size_t
UtpDrv::UtpHandler::utp_get_rb_size(void* data)
{
    return (static_cast<UtpHandler*>(data))->do_get_rb_size();
}

void
UtpDrv::UtpHandler::utp_state_change(void* data, int state)
{
    (static_cast<UtpHandler*>(data))->do_state_change(state);
}

void
UtpDrv::UtpHandler::utp_error(void* data, int errcode)
{
    (static_cast<UtpHandler*>(data))->do_error(errcode);
}

void
UtpDrv::UtpHandler::utp_overhead(void* data, bool send, size_t count, int type)
{
    (static_cast<UtpHandler*>(data))->do_overhead(send, count, type);
}

void
UtpDrv::UtpHandler::utp_incoming(void* data, UTPSocket* utp)
{
    (static_cast<UtpHandler*>(data))->do_incoming(utp);
}
