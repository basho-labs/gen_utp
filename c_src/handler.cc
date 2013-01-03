// -------------------------------------------------------------------
//
// handler.cc: abstract base class for driver handlers
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

#include "handler.h"
#include "globals.h"


using namespace UtpDrv;

UtpDrv::Handler::~Handler()
{
    port = 0;
}

void
UtpDrv::Handler::set_port(ErlDrvPort p)
{
    UTPDRV_TRACE("Handler::set_port\r\n");
    port = p;
    set_port_control_flags(port, PORT_CONTROL_FLAG_BINARY);
    port_status = port_started;
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
