#ifndef UTPDRV_UTP_LISTENER_H
#define UTPDRV_UTP_LISTENER_H

// -------------------------------------------------------------------
//
// utpdrv_listener.h: Erlang driver listener port for uTP
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

#include <cstring>
#include <map>
#include <queue>
#include "utpdrv_port.h"

namespace UtpDrv {

class Server;

struct SockAddrComp
{
    bool
    operator()(const SockAddr& s1, const SockAddr& s2) {
        if (s1.ss_family == s2.ss_family) {
            return memcmp(&s1, &s2, sizeof s1) < 0;
        } else {
            return s1.ss_family < s2.ss_family;
        }
    }
};

class Listener : public Port
{
public:
    Listener(Dispatcher& disp, int sock);
    ~Listener();

    ErlDrvSSizeT
    send(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    ErlDrvSSizeT
    peername(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    ErlDrvSSizeT
    close(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    void stop();

    void server_close(Server* server);

private:
    typedef std::map<SockAddr, Server*, SockAddrComp> ServerMap;
    ServerMap servers;
    ErlDrvMutex* sm_mutex;

    void do_incoming(UTPSocket* utp);
};

}


// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
