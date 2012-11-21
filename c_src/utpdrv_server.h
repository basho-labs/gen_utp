#ifndef UTPDRV_UTP_SERVER_H
#define UTPDRV_UTP_SERVER_H

// -------------------------------------------------------------------
//
// utpdrv_server.h: Erlang driver server port for uTP
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
#include "utpdrv_listener.h"

namespace UtpDrv {

class Server : public Port
{
public:
    Server(Dispatcher& disp, Listener& lr, UTPSocket* utp);
    ~Server();

    void incoming();

    void force_close();

private:
    Listener& lstnr;

    Server(const Server&);
    void operator=(const Server&);
};

}


// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
