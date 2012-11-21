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

#include <new>
#include <unistd.h>
#include "erl_driver.h"
#include "locker.h"
#include "utpdrv_dispatcher.h"

using namespace UtpDrv;

static int
utp_init()
{
    return Dispatcher::init();
}

static void
utp_finish()
{
    Dispatcher::finish();
}

static ErlDrvData
utp_start(ErlDrvPort port, char* command)
{
    Dispatcher* drv = new Dispatcher(port);
    drv->start();
    return reinterpret_cast<ErlDrvData>(drv);
}

static void
utp_stop(ErlDrvData drv_data)
{
    Dispatcher* drv = reinterpret_cast<Dispatcher*>(drv_data);
    drv->stop();
    delete drv;
}

static void
utp_check_timeouts(ErlDrvData drv_data)
{
    Dispatcher* drv = reinterpret_cast<Dispatcher*>(drv_data);
    drv->check_timeouts();
}

static ErlDrvSSizeT
utp_control(ErlDrvData drv_data, unsigned int command,
            char *buf, ErlDrvSizeT len, char **rbuf, ErlDrvSizeT rlen)
{
    Dispatcher* drv = reinterpret_cast<Dispatcher*>(drv_data);
    switch (command) {
    case UTP_CONNECT_START:
        return drv->connect_start(buf, len, rbuf, rlen);
    case UTP_CONNECT_VALIDATE:
        return drv->connect_validate(buf, len, rbuf, rlen);
    case UTP_LISTEN:
        return drv->listen(buf, len, rbuf, rlen);
    case UTP_SEND:
        return drv->send(buf, len, rbuf, rlen);
//    case UTP_RECV:
//        return drv->recv(buf, len, rbuf, rlen);
    case UTP_CLOSE:
        return drv->close(buf, len, rbuf, rlen);
    case UTP_SOCKNAME:
        return drv->sockname(buf, len, rbuf, rlen);
    case UTP_PEERNAME:
        return drv->peername(buf, len, rbuf, rlen);
    default:
        break;
    }
    return reinterpret_cast<ErlDrvSSizeT>(ERL_DRV_ERROR_GENERAL);
}

static void
utp_ready_input(ErlDrvData drv_data, ErlDrvEvent event)
{
    Dispatcher* drv = reinterpret_cast<Dispatcher*>(drv_data);
    drv->read_ready(reinterpret_cast<long>(event));
}

static void
utp_process_exit(ErlDrvData drv_data, ErlDrvMonitor* monitor)
{
    Dispatcher* drv = reinterpret_cast<Dispatcher*>(drv_data);
    drv->process_exit(monitor);
}

static void
utp_stop_select(ErlDrvEvent event, void*)
{
    long fd = reinterpret_cast<long>(event);
    ::close(fd);
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
    Dispatcher::drv_name,
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
    utp_process_exit,
    utp_stop_select,
};

extern "C" {
    DRIVER_INIT(utpdrv)
    {
        return &drv_entry;
    }
}
