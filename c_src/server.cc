// -------------------------------------------------------------------
//
// server.cc: uTP server port
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

#include "server.h"
#include "listener.h"
#include "globals.h"
#include "locker.h"


using namespace UtpDrv;

UtpDrv::Server::Server(int sock, const SockOpts& so) :
    UtpHandler(sock, so)
{
    UTPDRV_TRACER << "Server::Server " << this
                  << ", socket " << sock << UTPDRV_TRACE_ENDL;
    status = connected;
}

UtpDrv::Server::~Server()
{
    UTPDRV_TRACER << "Server::~Server " << this << UTPDRV_TRACE_ENDL;
}

void
UtpDrv::Server::do_send_to(const byte* p, size_t len,
                           const sockaddr* to, socklen_t slen)
{
    UTPDRV_TRACER << "Server::do_send_to " << this << UTPDRV_TRACE_ENDL;
    if (udp_sock != INVALID_SOCKET) {
        int index = 0;
        for (;;) {
            ssize_t count = send(udp_sock, p+index, len-index, 0);
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
UtpDrv::Server::do_incoming(UTPSocket* utp_sock)
{
    UTPDRV_TRACER << "Server::do_incoming " << this << UTPDRV_TRACE_ENDL;
    if (utp == 0) {
        utp = utp_sock;
        set_utp_callbacks();
        writable = true;
        status = connected;
    }
}
