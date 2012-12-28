#ifndef UTPDRV_MAIN_PORT_H
#define UTPDRV_MAIN_PORT_H

// -------------------------------------------------------------------
//
// main_port.h: primary port for uTP driver
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
#include "utp_port.h"
#include "drv_types.h"


namespace UtpDrv {

class MainPort : public Handler
{
public:
    explicit MainPort(ErlDrvPort p);
    ~MainPort();

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

    ErlDrvPort drv_port() const;

    void deselect(int fd) const;

    static void add_monitor(const ErlDrvMonitor& mon, Handler* h);
    static void del_monitor(ErlDrvPort port, ErlDrvMonitor& mon);

private:
    ErlDrvPort port;
    ErlDrvTermData owner;

    struct MonCompare {
        bool
        operator()(const ErlDrvMonitor& m1, const ErlDrvMonitor& m2) {
            return driver_compare_monitors(&m1, &m2) < 0;
        }
    };

    enum {
        UTP_IP = 1,
        UTP_FD,
        UTP_PORT,
        UTP_LIST,
        UTP_BINARY,
        UTP_INET,
        UTP_INET6,
        UTP_SEND_TMOUT
    };

    struct SockOpts {
        SockOpts();
        SockAddr addr;
        char addrstr[INET6_ADDRSTRLEN];
        long send_tmout;
        int fd;
        unsigned short port;
        UtpPort::DataDelivery delivery;
        bool inet6;
        bool addr_set;
    };
    void decode_sock_opts(const Binary& opts, SockOpts&);

    static ErlDrvMutex* map_mutex;
    typedef std::map<int, UtpPort*> FdMap;
    static FdMap fdmap;
    typedef std::map<ErlDrvMonitor, Handler*, MonCompare> MonMap;
    static MonMap monmap;

    ErlDrvSSizeT
    connect_start(const char* buf, ErlDrvSizeT len,
                  char** rbuf, ErlDrvSizeT rlen);

    ErlDrvSSizeT
    listen(const char* buf, ErlDrvSizeT len, char** rbuf, ErlDrvSizeT rlen);

    // prevent copies
    MainPort(const MainPort&);
    void operator=(const MainPort&);
};

}



// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
