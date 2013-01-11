#ifndef UTPDRV_HANDLER_H
#define UTPDRV_HANDLER_H

// -------------------------------------------------------------------
//
// handler.h: abstract base class for driver handlers
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

#include <new>
#include "erl_driver.h"
#include "libutp/utp.h"


namespace UtpDrv {

// Command values must match those defined in gen_utp.erl
enum Commands {
    UTP_LISTEN = 1,
    UTP_ACCEPT,
    UTP_CANCEL_ACCEPT,
    UTP_CONNECT_START,
    UTP_CONNECT_VALIDATE,
    UTP_CLOSE,
    UTP_SOCKNAME,
    UTP_PEERNAME,
    UTP_SETOPTS,
    UTP_GETOPTS,
    UTP_CANCEL_SEND,
    UTP_RECV,
    UTP_CANCEL_RECV
};

// Type for delivery of data from a port back to Erlang: binary or list
enum DeliveryMode {
    DATA_LIST,
    DATA_BINARY
};

class Handler
{
public:
    virtual ~Handler();

    virtual ErlDrvSSizeT
    control(unsigned command, const char* buf, ErlDrvSizeT len,
            char** rbuf, ErlDrvSizeT rlen) = 0;

    virtual void
    outputv(ErlIOVec& ev) = 0;

    virtual void stop() = 0;

    virtual void set_port(ErlDrvPort p);

    void* operator new(size_t s);
    void operator delete(void* p);

protected:
    Handler();
    explicit Handler(ErlDrvPort p);

    enum PortStatus {
        port_not_started,
        port_started,
        port_stopped
    };

    ErlDrvPort port;
    PortStatus port_status;
};

}



// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
