#ifndef UTPDRV_CLIENT_H
#define UTPDRV_CLIENT_H

// -------------------------------------------------------------------
//
// client.h: uTP client port
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
#include "utils.h"


namespace UtpDrv {

class Client : public UtpPort
{
public:
    Client(int sock, const Binary& ref, DataDelivery del, long send_timeout);
    ~Client();

    ErlDrvSSizeT
    control(unsigned command, const char* buf, ErlDrvSizeT len,
            char** rbuf, ErlDrvSizeT rlen);

    void connect_to(const SockAddr& addr);

private:
    ErlDrvSSizeT
    connect_validate(const char* buf, ErlDrvSizeT len,
                     char** rbuf, ErlDrvSizeT rlen);

    void do_incoming(UTPSocket* utp);

    // prevent copies
    Client(const Client&);
    void operator=(const Client&);
};

}


// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
