// -------------------------------------------------------------------
//
// handler.cc: abstract base class for driver handlers
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

#include "handler.h"
#include "globals.h"


using namespace UtpDrv;

UtpDrv::Handler::Handler() : port(0)
{
}

UtpDrv::Handler::Handler(ErlDrvPort p)
{
    set_port(p);
}

UtpDrv::Handler::~Handler()
{
    port = 0;
}

void
UtpDrv::Handler::set_port(ErlDrvPort p)
{
    UTPDRV_TRACER << "Handler::set_port " << this << UTPDRV_TRACE_ENDL;
    port = p;
    set_port_control_flags(port, PORT_CONTROL_FLAG_BINARY);
}

void
UtpDrv::Handler::process_exited(const ErlDrvMonitor*, ErlDrvTermData proc)
{
    ErlDrvTermData connected = driver_connected(port);
    if (proc == connected) {
        driver_failure_eof(port);
    }
}

void*
UtpDrv::Handler::operator new(size_t s)
{
    return driver_alloc(s);
}

void
UtpDrv::Handler::operator delete(void* p)
{
    driver_free(p);
}
