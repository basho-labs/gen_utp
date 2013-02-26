#ifndef UTPDRV_LISTENER_H
#define UTPDRV_LISTENER_H

// -------------------------------------------------------------------
//
// listener.h: uTP listen port
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

#include <list>
#include "socket_handler.h"
#include "utils.h"
#include "libutp/utp.h"


namespace UtpDrv {

class Server;

class Listener : public SocketHandler
{
public:
    Listener(int sock, const SockOpts& so);
    ~Listener();

    ErlDrvSSizeT
    control(unsigned command, const char* buf, ErlDrvSizeT len,
            char** rbuf, ErlDrvSizeT rlen);

    void outputv(ErlIOVec& ev);

    void stop();

    void process_exit(ErlDrvMonitor* monitor);

    void input_ready();

protected:
    ErlDrvSSizeT close(const char* buf, ErlDrvSizeT len,
                       char** rbuf, ErlDrvSizeT rlen);

    ErlDrvSSizeT peername(const char* buf, ErlDrvSizeT len,
                          char** rbuf, ErlDrvSizeT rlen);

    ErlDrvSSizeT accept(const char* buf, ErlDrvSizeT len,
                        char** rbuf, ErlDrvSizeT rlen);

    ErlDrvSSizeT cancel_accept(const char* buf, ErlDrvSizeT len,
                               char** rbuf, ErlDrvSizeT rlen);

private:
    struct Acceptor {
        ErlDrvTermData caller;
        Binary ref;
    };
    typedef std::list<Acceptor> AcceptorQueue;

    AcceptorQueue acceptor_queue;
    SockAddr my_addr;
    ErlDrvMutex* queue_mutex;

    void do_write(byte* bytes, size_t count);
    void do_incoming(UTPSocket* utp);

    // prevent copies
    Listener(const Listener&);
    void operator=(const Listener&);
};

}


// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
