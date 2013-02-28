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
    error_code(0), writable(false), sender_waiting(false), receiver_waiting(false),
    eof_seen(false)
{
}

UtpDrv::UtpHandler::~UtpHandler()
{
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
        UTP_SetSockopt(utp, SO_SNDBUF, UTP_SNDBUF_DEFAULT);
        UTP_SetSockopt(utp, SO_RCVBUF, UTP_RECBUF_DEFAULT);
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
    case UTP_SETOPTS:
        return setopts(buf, len, rbuf, rlen);
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
    size_t write_total = 0;
    if (writable) {
        if (ev.size > 0) {
            if (sockopts.packet > 0) {
                ErlDrvBinary* pbin = driver_alloc_binary(sockopts.packet);
                union {
                    char* p1;
                    uint16_t* p2;
                    uint32_t* p4;
                };
                p1 = pbin->orig_bytes;
                switch (sockopts.packet) {
                case 1:
                    *p1 = ev.size & 0xFF;
                    break;
                case 2:
                    *p2 = htons(ev.size & 0xFFFF);
                    break;
                case 4:
                    *p4 = htonl(ev.size & 0xFFFFFFFF);
                    break;
                }
                write_queue.push_back(pbin);
                write_total += sockopts.packet;
            }
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
            write_total += ev.size;
        }
        {
            MutexLocker lock(utp_mutex);
            writable = UTP_Write(utp, write_total);
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
    ErlDrvSizeT qsize = driver_sizeq(port);
    if (qsize != 0) {
        driver_deq(port, qsize);
    }
    if (status == destroying) {
        delete this;
    } else {
        status = stopped;
        if (selected) {
            // TODO: this is bad -- the port is closing but our udp socket
            // is still in the driver select set. We can't deselect it here
            // because there might still be uTP traffic occurring between
            // our utp socket and the remote side.
            UTPDRV_TRACER << "UtpHandler::stop: deselecting "
                          << udp_sock << " for " << this << UTPDRV_TRACE_ENDL;
        }
    }
}

ErlDrvSSizeT
UtpDrv::UtpHandler::setopts(const char* buf, ErlDrvSizeT len,
                            char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "UtpHandler::setopts " << this << UTPDRV_TRACE_ENDL;
    int saved_sndbuf = sockopts.sndbuf;
    int saved_recbuf = sockopts.recbuf;
    ErlDrvSSizeT result = SocketHandler::setopts(buf, len, rbuf, rlen);
    if (saved_sndbuf != sockopts.sndbuf) {
        UTP_SetSockopt(utp, SO_SNDBUF, sockopts.sndbuf);
    }
    if (saved_recbuf != sockopts.recbuf) {
        UTP_SetSockopt(utp, SO_RCVBUF, sockopts.recbuf);
    }
    return result;
}

ErlDrvSSizeT
UtpDrv::UtpHandler::close(const char* buf, ErlDrvSizeT len,
                          char** rbuf, ErlDrvSizeT rlen)
{
    UTPDRV_TRACER << "UtpHandler::close " << this
                  << ", status = " << status << ", close pending = "
                  << close_pending << ", eof_seen = " << eof_seen
                  << UTPDRV_TRACE_ENDL;
    const char* retval = "ok";
    if (!close_pending &&
        (eof_seen || (status != closing && status != destroying))) {
        MutexLocker lock(utp_mutex);
        status = closing;
        close_pending = true;
        eof_seen = false;
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
        decoder.ulongval(length);
        decoder.type(type, size);
        if (type != ERL_BINARY_EXT) {
            return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
        }
        ref.decode(decoder, size);
    } catch (const EiError&) {
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    ErlDrvTermData local_caller = driver_caller(port);
    Receiver rcvr(false, local_caller, ref);
    ErlDrvSizeT qsize = 1; // any non-zero value will do
    bool sent = emit_read_buffer(length, rcvr, qsize);
    if (sent && qsize == 0) {
        MutexLocker lock(utp_mutex);
        UTP_RBDrained(utp);
    } if (!sent) {
        MutexLocker lock(utp_mutex);
        caller_ref.swap(ref);
        caller = local_caller;
        recv_len = length;
        receiver_waiting = true;
    }
    EiEncoder encoder;
    encoder.atom("wait");
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
    reset_waiting_recv();
    return 0;
}

void
UtpDrv::UtpHandler::close_utp()
{
    UTPDRV_TRACER << "UtpHandler::close_utp " << this << UTPDRV_TRACE_ENDL;
    if (utp != 0 && status != destroying) {
        if (write_queue.size() != 0) {
            return;
        }
        status = closing;
        UTP_Close(utp);
        utp = 0;
    }
}

void
UtpDrv::UtpHandler::reset_waiting_recv()
{
    receiver_waiting = false;
    recv_len = 0;
    caller_ref.reset();
    caller = 0;
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
        ErlDrvSizeT qsize = 1; // any non-zero value will do
        char* buf = const_cast<char*>(reinterpret_cast<const char*>(bytes));
        driver_enq(port, buf, count);
        read_count.push_back(count);
        if (sockopts.active == ACTIVE_FALSE) {
            if (receiver_waiting) {
                Receiver rcvr(false, caller, caller_ref);
                if (emit_read_buffer(recv_len, rcvr, qsize)) {
                    reset_waiting_recv();
                }
            }
        } else {
            Receiver rcvr;
            emit_read_buffer(0, rcvr, qsize);
        }
        if (qsize == 0) {
            UTP_RBDrained(utp);
        }
    }
}

void
UtpDrv::UtpHandler::do_write(byte* bytes, size_t count)
{
    UTPDRV_TRACER << "UtpHandler::do_write " << this
                  << ": writing " << count << " bytes" << UTPDRV_TRACE_ENDL;
    if (count == 0) return;
    write_queue.pop_bytes(bytes, count);
}

size_t
UtpDrv::UtpHandler::do_get_rb_size()
{
    UTPDRV_TRACER << "UtpHandler::do_get_rb_size " << this << UTPDRV_TRACE_ENDL;
    return status == connected ? driver_sizeq(port) : 0;
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
        write_queue.clear();
        if (status != stopped) {
            close_utp();
        }
        writable = false;
        eof_seen = true;
        break;

    case UTP_STATE_WRITABLE:
        writable = true;
        {
            size_t sz = write_queue.size();
            if (sz != 0) {
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
        if (close_pending) {
            close_utp();
        }
        break;

    case UTP_STATE_CONNECT:
        if (status == connect_pending && caller_ref) {
            ErlDrvTermData term[] = {
                ERL_DRV_EXT2TERM, caller_ref, caller_ref.size(),
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("ok")),
                ERL_DRV_TUPLE, 2,
            };
            driver_output_term(port, term, sizeof term/sizeof *term);
        }
        status = connected;
        writable = true;
        break;

    case UTP_STATE_DESTROYING:
        if (selected) {
            UTPDRV_TRACER << "UtpHandler::do_state_change: deselecting "
                          << udp_sock << " for " << this << UTPDRV_TRACE_ENDL;
            MainHandler::stop_input(udp_sock);
            selected = false;
        }
        if (status == closing) {
            if (close_pending && caller_ref) {
                UTPDRV_TRACER << "UtpHandler::do_state_change: "
                              << "sending close message to caller for " << this
                              << UTPDRV_TRACE_ENDL;
                ErlDrvTermData term[] = {
                    ERL_DRV_EXT2TERM, caller_ref, caller_ref.size(),
                    ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("ok")),
                    ERL_DRV_TUPLE, 2,
                };
                if (caller != driver_term_nil) {
                    driver_send_term(port, caller, term, sizeof term/sizeof *term);
                } else {
                    driver_output_term(port, term, sizeof term/sizeof *term);
                }
                caller = driver_term_nil;
                close_pending = false;
            } else if (sockopts.active != ACTIVE_FALSE) {
                emit_closed_message();
            }
            writable = false;
            status = destroying;
            eof_seen = false;
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
            driver_output_term(port, term, sizeof term/sizeof *term);
        }
        break;
    case connected:
        if (errcode == ECONNRESET) {
            close_utp();
        } else if (sockopts.active != ACTIVE_FALSE) {
            ErlDrvTermData term[] = {
                ERL_DRV_ATOM,
                driver_mk_atom(const_cast<char*>("utp_error")),
                ERL_DRV_PORT, driver_mk_port(port),
                ERL_DRV_ATOM, driver_mk_atom(erl_errno_id(errcode)),
                ERL_DRV_TUPLE, 3
            };
            driver_output_term(port, term, sizeof term/sizeof *term);
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
