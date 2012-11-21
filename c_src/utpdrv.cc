// -------------------------------------------------------------------
//
// utpdrv.cc: wrapper/driver for libutp functions
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

#include <cstdio>
#include <new>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include "erl_driver.h"
#include "ei.h"
#include "libutp/utp.h"
#include "locker.h"


static char* utp_drv_name = const_cast<char*>("utpdrv");
const unsigned long timeout_check = 100;

typedef sockaddr_storage SockAddr;

enum UtpCommands {
    UTP_CONNECT = 1,
    UTP_CLOSE
};

//--

class UtpDriver
{
public:
    explicit UtpDriver(ErlDrvPort port);
    ~UtpDriver();

    static ErlDrvMutex* utp_mutex;

    void set_timer() {
        driver_set_timer(drv_port, timeout_check);
    }

    void stop_timer() {
        driver_cancel_timer(drv_port);
    }

    void check_timeouts();

    ErlDrvSSizeT connect(char *buf, ErlDrvSizeT len,
                         char **rbuf, ErlDrvSizeT rlen);

    void do_send_to(const byte* p, size_t len,
                    const sockaddr* to, socklen_t slen);
    void do_read(const byte* bytes, size_t count);
    void do_write(byte* bytes, size_t count);
    size_t do_get_rb_size();
    void do_state_change(int state);
    void do_error(int errcode);
    void do_overhead(bool send, size_t count, int type);
    void do_incoming(UTPSocket* utp);

    static void send_to(void* data, const byte* p, size_t len,
                        const sockaddr* to, socklen_t slen)
    {
        (static_cast<UtpDriver*>(data))->do_send_to(p, len, to, slen);
    }
    static void utp_read(void* data, const byte* bytes, size_t count)
    {
        (static_cast<UtpDriver*>(data))->do_read(bytes, count);
    }
    static void utp_write(void* data, byte* bytes, size_t count)
    {
        (static_cast<UtpDriver*>(data))->do_write(bytes, count);
    }
    static size_t utp_get_rb_size(void* data)
    {
        return (static_cast<UtpDriver*>(data))->do_get_rb_size();
    }
    static void utp_state_change(void* data, int state)
    {
        (static_cast<UtpDriver*>(data))->do_state_change(state);
    }
    static void utp_error(void* data, int errcode)
    {
        (static_cast<UtpDriver*>(data))->do_error(errcode);
    }
    static void utp_overhead(void* data, bool send, size_t count, int type)
    {
        (static_cast<UtpDriver*>(data))->do_overhead(send, count, type);
    }
    static void utp_incoming(void* data, UTPSocket* utp)
    {
        (static_cast<UtpDriver*>(data))->do_incoming(utp);
    }

private:
    int open_udp_socket();
    void set_callbacks();
    void output_error_atom(const char* errstr, const ErlDrvBinary* b);

    static bool addrport_to_sockaddr(const char* addr, unsigned short port,
                                     SockAddr& sa);
    static bool sockaddr_to_addrport(const SockAddr& sa, socklen_t slen,
                                     char* addr, size_t addrlen,
                                     unsigned short& port);

    ErlDrvPort drv_port;
    ErlDrvTermData owner;
    UTPSocket* utp;
    int udp_sock;
};

ErlDrvMutex* UtpDriver::utp_mutex;

UtpDriver::UtpDriver(ErlDrvPort port) : drv_port(port)
{
    owner = driver_caller(drv_port);
    set_port_control_flags(drv_port, PORT_CONTROL_FLAG_BINARY);
}

UtpDriver::~UtpDriver()
{
}

void
UtpDriver::check_timeouts()
{
    Locker lock(utp_mutex);
    UTP_CheckTimeouts();
    driver_set_timer(drv_port, timeout_check);
}

ErlDrvSSizeT
UtpDriver::connect(char *buf, ErlDrvSizeT len, char **rbuf, ErlDrvSizeT rlen)
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
    if (!addrport_to_sockaddr(addrstr, port, addr)) {
        driver_free_binary(from);
        return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_BADARG);
    }

    int err = open_udp_socket();
    if (err != 0) {
        output_error_atom(erl_errno_id(err), from);
        driver_free_binary(from);
        return 0;
    }
    ErlDrvEvent ev = reinterpret_cast<ErlDrvEvent>(udp_sock);
    if (driver_select(drv_port, ev, ERL_DRV_READ|ERL_DRV_USE, 1) != 0) {
        output_error_atom("driver_error", from);
        ::close(udp_sock);
        driver_free_binary(from);
        return 0;
    }
    {
        Locker lock(utp_mutex);
        utp = UTP_Create(&UtpDriver::send_to, this,
                         reinterpret_cast<sockaddr*>(&addr), addr.ss_len);
        set_callbacks();
        UTP_Connect(utp);
    }
    return 0;
}

void
UtpDriver::read_ready(int fd)
{
    FdMap::iterator it;
    {
        Locker lock(map_mutex);
        it = fdmap.find(fd);
    }
    if (it != fdmap.end()) {
        byte buf[4096];
        SockAddr addr;
        socklen_t salen = sizeof addr;
        int len = recvfrom(fd, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&addr), &salen);
        if (len > 0) {
            Locker lock(utp_mutex);
            UTP_IsIncomingUTP(&UtpDriver::utp_incoming, &UtpDriver::send_to, it->second,
                              buf, len, reinterpret_cast<sockaddr*>(&addr), salen);
        }
    }
}

void
UtpDriver::do_send_to(const byte* p, size_t len, const sockaddr* to, socklen_t slen)
{
    printf("called do_send_to for size %ld\r\n", len);
    fflush(stdout);
    if (sendto(udp_sock, p, len, 0, to, slen) < 0) {
        error_code = errno;
    }
}

void
UtpDriver::do_read(const byte* bytes, size_t count)
{
    printf("called do_read for size %ld\r\n", count);
    fflush(stdout);
#if 0
    ErlNifBinary bin;
    enif_alloc_binary(count, &bin);
    memcpy(bin.data, bytes, count);
    ERL_NIF_TERM bin_term = enif_make_binary(msg_env, &bin);
    enif_release_binary(&bin);
    ERL_NIF_TERM msg = enif_make_tuple3(msg_env, ATOM_READ,
                                        copy_ref(msg_env), bin_term);
    send_msg(msg);
#endif
}

void
UtpDriver::do_write(byte* bytes, size_t count)
{
    printf("called do_write for size %ld\r\n", count);
    fflush(stdout);
#if 0
    while (count > 0) {
        ErlNifBinary& bin = writes.front();
        if (bin.size <= count) {
            memcpy(bytes, bin.data, bin.size);
            write_count -= bin.size;
            writes.pop_front();
            count -= bin.size;
            bytes += bin.size;
        } else {
            memcpy(bytes, bin.data, count);
            ErlNifBinary newbin;
            size_t newsz = bin.size - count;
            enif_alloc_binary(newsz, &newbin);
            memcpy(newbin.data, bin.data + count, newsz);
            writes.pop_front();
            writes.push_front(newbin);
            count = 0;
        }
        enif_release_binary(&bin);
    }
#endif
}

size_t
UtpDriver::do_get_rb_size()
{
    return 0;
}

void
UtpDriver::do_state_change(int s)
{
    printf("called do_state_change for state %d\r\n", s);
    fflush(stdout);
    if (utp_state == s) return;
    utp_state = s;
    switch (utp_state) {
    case UTP_STATE_EOF:
        connected = false;
        break;

    case UTP_STATE_WRITABLE:
        break;

    case UTP_STATE_CONNECT:
        assert(sock_state == sock_connecting);
        sock_state = sock_connected;
        connected = true;
        my_port = driver_create_port(drv_port, drv_owner, utp_drv_name,
                                     reinterpret_cast<ErlDrvData>(this));
        {
            ErlDrvTermData ext = reinterpret_cast<ErlDrvTermData>(from->orig_bytes);
            ErlDrvTermData term[] = {
                ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("ok")),
                ERL_DRV_PORT, driver_mk_port(my_port),
                ERL_DRV_EXT2TERM, ext, from->orig_size,
                ERL_DRV_TUPLE, 3,
            };
            driver_send_term(drv_port, drv_owner, term, sizeof term/sizeof *term);
            driver_free_binary(from);
        }
        break;

    case UTP_STATE_DESTROYING:
        break;
    }
}

void
UtpDriver::do_error(int errcode)
{
    printf("called do_error for error %d\r\n", errcode);
    fflush(stdout);
    error_code = errcode;
    switch (sock_state) {
    case sock_connecting:
        sock_state = sock_error;
        send_error_atom(drv_port, drv_owner, erl_errno_id(error_code), from);
        driver_free_binary(from);
        delete this;
        break;
    case sock_connected:
    case sock_closing:
    case sock_closed:
    case sock_error:
        break;
    }
}

void
UtpDriver::do_overhead(bool send, size_t count, int type)
{
//    printf("called do_overhead for size %ld type %d send %d\r\n", count, type, send);
//    fflush(stdout);
}

void
UtpDriver::do_incoming(UTPSocket* utp)
{
    printf("called do_incoming");
    fflush(stdout);
#if 0
    sock = utp;
    set_callbacks();
    SockAddr addr;
    socklen_t addrlen;
    UTP_GetPeerName(utp, reinterpret_cast<sockaddr*>(&addr), &addrlen);
    char str[INET6_ADDRSTRLEN];
    unsigned short port;
    sockaddr_to_addrport(*reinterpret_cast<sockaddr*>(&addr), addrlen,
                         str, sizeof str, port);
    ERL_NIF_TERM res = enif_make_resource(msg_env, this);
    ERL_NIF_TERM addr_term = enif_make_string(msg_env, str, ERL_NIF_LATIN1);
    ERL_NIF_TERM port_term = enif_make_uint(msg_env, port);
    ERL_NIF_TERM addrport = enif_make_tuple2(msg_env, addr_term, port_term);
    ERL_NIF_TERM msg = enif_make_tuple4(msg_env, ATOM_UTP, res, copy_ref(msg_env), addrport);
    send_msg(msg);
#endif
}

int
UtpDriver::open_udp_socket()
{
    if ((udp_sock = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) >= 0) {
        int flags = fcntl(udp_sock, F_GETFL);
        flags |= O_NONBLOCK;
        fcntl(udp_sock, F_SETFL, flags);
    }
    return errno;
}

void
UtpDriver::set_callbacks()
{
    if (utp) {
        UTPFunctionTable funcs = {
            &UtpDriver::utp_read,
            &UtpDriver::utp_write,
            &UtpDriver::utp_get_rb_size,
            &UtpDriver::utp_state_change,
            &UtpDriver::utp_error,
            &UtpDriver::utp_overhead,
        };
        UTP_SetCallbacks(utp, &funcs, this);
    }
}

void
UtpDriver::output_error_atom(const char* errstr, const ErlDrvBinary* b)
{
    ErlDrvTermData bin_ext = reinterpret_cast<ErlDrvTermData>(b->orig_bytes);
    ErlDrvTermData term[] = {
        ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("error")),
        ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>(errstr)),
        ERL_DRV_EXT2TERM, bin_ext, b->orig_size,
        ERL_DRV_TUPLE, 3,
    };
    driver_output_term(drv_port, term, sizeof term/sizeof *term);
}

bool
UtpDriver::addrport_to_sockaddr(const char* addr, unsigned short port,
                                SockAddr& sa_arg)
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
UtpDriver::sockaddr_to_addrport(const SockAddr& sa_arg, socklen_t slen,
                                char* addr, size_t addrlen, unsigned short& port)
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

//--------------------------------------------------------------------

static int
utp_init(void)
{
    UtpDriver::utp_mutex = erl_drv_mutex_create(utp_drv_name);
    return 0;
}

static void
utp_finish(void)
{
    erl_drv_mutex_destroy(UtpDriver::utp_mutex);
}

static ErlDrvData
utp_start(ErlDrvPort port, char* command)
{
    void* p = driver_alloc(sizeof(UtpDriver));
    UtpDriver* drv = new(p) UtpDriver(port);
    drv->set_timer();
    return reinterpret_cast<ErlDrvData>(drv);
}

static void
utp_stop(ErlDrvData drv_data)
{
    UtpDriver* drv = reinterpret_cast<UtpDriver*>(drv_data);
    drv->stop_timer();
    drv->~UtpDriver();
    driver_free(drv);
}

static void
utp_check_timeouts(ErlDrvData drv_data)
{
    UtpDriver* drv = reinterpret_cast<UtpDriver*>(drv_data);
    drv->check_timeouts();
}

static ErlDrvSSizeT
utp_control(ErlDrvData drv_data, unsigned int command,
            char *buf, ErlDrvSizeT len, char **rbuf, ErlDrvSizeT rlen)
{
    UtpDriver* drv = reinterpret_cast<UtpDriver*>(drv_data);
    switch (command) {
    case UTP_CONNECT:
        return drv->connect(buf, len, rbuf, rlen);
    case UTP_CLOSE:
        break;
    default:
        break;
    }
    return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_GENERAL);
}

static void
utp_ready_input(ErlDrvData drv_data, ErlDrvEvent event)
{
//    UtpDriver* drv = reinterpret_cast<UtpDriver*>(drv_data);
//    UtpDriver::read_ready(reinterpret_cast<int>(event));
}

static void
utp_stop_select(ErlDrvEvent event, void*)
{
    ::close(reinterpret_cast<int>(event));
}

static ErlDrvEntry drv_entry = {
    utp_init,
    utp_start,
    utp_stop,
    0,//void (*output)(ErlDrvData drv_data, char *buf, ErlDrvSizeT len);
                                /* called when we have output from erlang to
                                   the port */
    utp_ready_input,
                                /* called when we have input from one of
                                   the driver's handles */
    0,//void (*ready_output)(ErlDrvData drv_data, ErlDrvEvent event);
                                /* called when output is possible to one of
                                   the driver's handles */
    utp_drv_name,
    utp_finish,
    0,
    utp_control,
    utp_check_timeouts,
    0,//void (*outputv)(ErlDrvData drv_data, ErlIOVec *ev);
                                /* called when we have output from erlang
                                   to the port */
    0,//void (*ready_async)(ErlDrvData drv_data, ErlDrvThreadData thread_data);
    0,//void (*flush)(ErlDrvData drv_data);
                                /* called when the port is about to be
                                   closed, and there is data in the
                                   driver queue that needs to be flushed
                                   before 'stop' can be called */
    0,
    0,//void (*event)(ErlDrvData drv_data, ErlDrvEvent event,
      //            ErlDrvEventData event_data);
                                /* Called when an event selected by
                                   driver_event() has occurred */
    ERL_DRV_EXTENDED_MARKER,
    ERL_DRV_EXTENDED_MAJOR_VERSION,
    ERL_DRV_EXTENDED_MINOR_VERSION,
    ERL_DRV_FLAG_USE_PORT_LOCKING,
    0,
    0,//void (*process_exit)(ErlDrvData drv_data, ErlDrvMonitor *monitor);
                                /* Called when a process monitor fires */
    utp_stop_select,
};

extern "C" {
    DRIVER_INIT(utpdrv)
    {
        return &drv_entry;
    }
}

#if 0
static ERL_NIF_TERM
close(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    UtpSock* sock;
    if (enif_get_resource(env, argv[0], UTP_SOCK_RESOURCE, (void**)&sock)) {
        sock->close();
        return ATOM_OK;
    }
    return enif_make_badarg(env);
}

static ERL_NIF_TERM
connect_impl(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    SockAddr addr;
    unsigned int port;
    ErlNifPid pid;

    if (!enif_get_uint(env, argv[1], &port) ||
        !get_sockaddr(env, argv[0], port, addr) ||
        !enif_get_local_pid(env, argv[2], &pid)) {
        return enif_make_badarg(env);
    }
    void* buf = enif_alloc_resource(UTP_SOCK_RESOURCE, sizeof(UtpSock));
    UtpSock* sock = new(buf) UtpSock(pid);
    UTPSocket* utp;
    {
        Locker lock(utp_mutex);
        utp = UTP_Create(&UtpSock::send_to, sock,
                         reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    }
    sock->connect(utp);
    ERL_NIF_TERM res = enif_make_resource(env, sock);
    return enif_make_tuple3(env, ATOM_OK, res, sock->copy_ref(env));
}

static ERL_NIF_TERM
destroy(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    UtpSock* sock;
    if (enif_get_resource(env, argv[0], UTP_SOCK_RESOURCE, (void**)&sock)) {
        sock->~UtpSock();
        enif_release_resource(sock);
        return ATOM_OK;
    }
    return enif_make_badarg(env);
}

static ERL_NIF_TERM
incoming_impl(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    SockAddr addr;
    unsigned int port;
    ErlNifBinary bin;
    ErlNifPid pid;

    if (!enif_get_uint(env, argv[1], &port) ||
        !get_sockaddr(env, argv[0], port, addr) ||
        !enif_inspect_iolist_as_binary(env, argv[2], &bin) ||
        !enif_get_local_pid(env, argv[3], &pid)) {
        return enif_make_badarg(env);
    }
    void* buf = enif_alloc_resource(UTP_SOCK_RESOURCE, sizeof(UtpSock));
    UtpSock* sock = new(buf) UtpSock(pid);
    Locker lock(utp_mutex);
    bool result = UTP_IsIncomingUTP(&UtpSock::utp_incoming, &UtpSock::send_to, sock, bin.data, bin.size,
                                    reinterpret_cast<sockaddr*>(&addr), sizeof addr);
    if (result) {
        return ATOM_TRUE;
    } else {
        sock->~UtpSock();
        enif_release_resource(sock);
        return ATOM_FALSE;
    }
}

static ERL_NIF_TERM
write(ErlNifEnv* env, int argc, const ERL_NIF_TERM argv[])
{
    UtpSock* sock;
    char str[6];
    if (enif_get_resource(env, argv[0], UTP_SOCK_RESOURCE, (void**)&sock)) {
        ErlNifBinary bin;
        if (enif_inspect_iolist_as_binary(env, argv[1], &bin) &&
            enif_get_atom(env, argv[2], str, sizeof str, ERL_NIF_LATIN1)) {
            bool writable = (strcmp(str, "true") == 0);
            return sock->write(bin, writable) ? ATOM_TRUE : ATOM_FALSE;
        }
    }
    return enif_make_badarg(env);
}

static bool
get_sockaddr(ErlNifEnv* env, ERL_NIF_TERM arg, unsigned short port, SockAddr& addr)
{
    char str[INET6_ADDRSTRLEN];
    memset(&addr, 0, sizeof addr);
    return enif_get_string(env, arg, str, sizeof str, ERL_NIF_LATIN1) != 0 &&
        UtpSock::addrport_to_sockaddr(str, port, *reinterpret_cast<sockaddr*>(&addr));
}

#endif
