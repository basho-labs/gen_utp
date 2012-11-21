#ifndef UTPDRV_UTPDRV_DISPATCHER_H
#define UTPDRV_UTPDRV_DISPATCHER_H

// -------------------------------------------------------------------
//
// utpdrv_dispatcher.h: uTP driver dispatcher
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

#include <map>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include "erl_driver.h"


namespace UtpDrv {

typedef sockaddr_storage SockAddr;

// Command values must match those defined in gen_utp.erl
enum Commands {
    UTP_CONNECT_START = 1,
    UTP_CONNECT_VALIDATE,
    UTP_LISTEN,
    UTP_SEND,
    UTP_RECV,
    UTP_CLOSE,
    UTP_SOCKNAME,
    UTP_PEERNAME
};

const int INVALID_SOCKET = -1;

class Port;

class Dispatcher
{
public:
    explicit Dispatcher(ErlDrvPort port);
    virtual ~Dispatcher();

    void* operator new(size_t s);
    void operator delete(void* p);

    void start();
    virtual void stop();
    void check_timeouts();
    void read_ready(int fd);
    void deselect(int fd);

    ErlDrvSSizeT connect_start(char* buf, ErlDrvSizeT len,
                               char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    connect_validate(const char* buf, ErlDrvSizeT len,
                     char** rbuf, ErlDrvSizeT rlen);

    ErlDrvSSizeT listen(char* buf, ErlDrvSizeT len,
                        char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    close(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    send(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    sockname(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual ErlDrvSSizeT
    peername(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    virtual void
    process_exit(ErlDrvMonitor* monitor);

    ErlDrvPort
    create_port(ErlDrvTermData owner, Port* data, bool do_set_port = true);

    static int init();
    static void finish();

    static char* drv_name;

protected:
    typedef std::map<int, Port*> FdMap;
    static FdMap fdmap;
    static ErlDrvMutex* map_mutex;
    static ErlDrvMutex* utp_mutex;
    static ErlDrvMutex* drv_mutex;
    ErlDrvPort drv_port;
    ErlDrvTermData drv_owner;
    ErlDrvPDL pdl;

    void output_error_atom(const char* err, const ErlDrvBinary* b);
    bool addrport_to_sockaddr(const char* addr, unsigned short port,
                              SockAddr& sa_arg, socklen_t& slen);
    bool sockaddr_to_addrport(const SockAddr& sa_arg,
                              socklen_t slen,
                              char* addr, size_t addrlen,
                              unsigned short& port);

private:
    int open_udp_socket(int& sock, unsigned short port = 0);
};

}

// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
