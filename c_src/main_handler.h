#ifndef UTPDRV_MAIN_HANDLER_H
#define UTPDRV_MAIN_HANDLER_H

// -------------------------------------------------------------------
//
// main_handler.h: handler for primary uTP driver port
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
#include "handler.h"
#include "utils.h"
#include "utp_handler.h"
#include "drv_types.h"


namespace UtpDrv {

class MainHandler : public Handler
{
public:
    explicit MainHandler(ErlDrvPort p);
    ~MainHandler();

    static int driver_init();
    static void driver_finish();

    void check_utp_timeouts() const;

    ErlDrvSSizeT
    control(unsigned command, const char* buf, ErlDrvSizeT len,
            char** rbuf, ErlDrvSizeT rlen);

    void start();
    void stop();
    void ready_input(long fd);
    void outputv(ErlIOVec& ev);
    void process_exit(ErlDrvMonitor* monitor);

    static ErlDrvPort drv_port();

    static void start_input(int fd, SocketHandler* handler);
    static void stop_input(int& fd);

    static void add_monitor(const ErlDrvMonitor& mon, Handler* h);
    static void del_monitor(ErlDrvPort port, ErlDrvMonitor& mon);

private:
    // singleton
    static MainHandler* main_handler;

    ErlDrvTermData owner;

    struct MonCompare {
        bool
        operator()(const ErlDrvMonitor& m1, const ErlDrvMonitor& m2) {
            return driver_compare_monitors(&m1, &m2) < 0;
        }
    };

    ErlDrvMutex* map_mutex;

    typedef std::map<int, SocketHandler*> FdMap;
    FdMap fdmap;
    typedef std::map<ErlDrvMonitor, Handler*, MonCompare> MonMap;
    MonMap monmap;

    ErlDrvSSizeT
    connect_start(const char* buf, ErlDrvSizeT len,
                  char** rbuf, ErlDrvSizeT rlen);

    ErlDrvSSizeT
    listen(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    void select(int fd, SocketHandler* handler);
    void deselect(int& fd);

    void add_mon(const ErlDrvMonitor& mon, Handler* h);
    void del_mon(ErlDrvPort port, ErlDrvMonitor& mon);

    // prevent copies
    MainHandler(const MainHandler&);
    void operator=(const MainHandler&);
};

}



// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
