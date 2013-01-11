// -------------------------------------------------------------------
//
// utpdrv.cc: driver entry points for libutp functions
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

#include <unistd.h>
#include "erl_driver.h"
#include "globals.h"
#include "main_handler.h"


using namespace UtpDrv;

static int
utp_init()
{
    return MainHandler::driver_init();
}

static void
utp_finish()
{
    MainHandler::driver_finish();
}

static ErlDrvData
utp_start(ErlDrvPort port, char* command)
{
    MainHandler* drv = new MainHandler(port);
    drv->start();
    return reinterpret_cast<ErlDrvData>(drv);
}

static void
utp_stop(ErlDrvData drv_data)
{
    Handler* drv = reinterpret_cast<Handler*>(drv_data);
    drv->stop();
}

static void
utp_check_timeouts(ErlDrvData drv_data)
{
    MainHandler* drv = reinterpret_cast<MainHandler*>(drv_data);
    drv->check_utp_timeouts();
}

static void
utp_outputv(ErlDrvData drv_data, ErlIOVec *ev)
{
    Handler* drv = reinterpret_cast<Handler*>(drv_data);
    drv->outputv(*ev);
}

static ErlDrvSSizeT
utp_control(ErlDrvData drv_data, unsigned int command,
            char *buf, ErlDrvSizeT len, char **rbuf, ErlDrvSizeT rlen)
{
    Handler* drv = reinterpret_cast<Handler*>(drv_data);
    return drv->control(command, buf, len, rbuf, rlen);
}

static void
utp_ready_input(ErlDrvData drv_data, ErlDrvEvent event)
{
    MainHandler* drv = reinterpret_cast<MainHandler*>(drv_data);
    drv->ready_input(reinterpret_cast<long>(event));
}

static void
utp_process_exit(ErlDrvData drv_data, ErlDrvMonitor* monitor)
{
    MainHandler* drv = reinterpret_cast<MainHandler*>(drv_data);
    drv->process_exit(monitor);
}

static void
utp_stop_select(ErlDrvEvent event, void*)
{
    long fd = reinterpret_cast<long>(event);
    UTPDRV_TRACER << "utp_stop_select: closing fd " << fd << UTPDRV_TRACE_ENDL;
    ::close(fd);
}

static ErlDrvEntry drv_entry = {
    utp_init,
    utp_start,
    utp_stop,
    0,
    utp_ready_input,
    0,
    UtpDrv::drv_name,
    utp_finish,
    0,
    utp_control,
    utp_check_timeouts,
    utp_outputv,
    0,
    0,
    0,
    0,
    static_cast<int>(ERL_DRV_EXTENDED_MARKER),
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
