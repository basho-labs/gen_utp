#ifndef UTPDRV_GLOBALS_H
#define UTPDRV_GLOBALS_H

// -------------------------------------------------------------------
//
// globals.h: uTP driver global variables
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

#include <iostream>
#include "erl_driver.h"


#define UTPDRV_DEBUG 0
#if UTPDRV_DEBUG
#define UTPDRV_TRACER if (false) ; else std::cerr
#else
#define UTPDRV_TRACER if (true) ; else std::cerr
#endif
#define UTPDRV_TRACE_ENDL "\r" << std::endl

namespace UtpDrv {

const int INVALID_SOCKET = -1;

extern char* drv_name;

extern ErlDrvMutex* utp_mutex;

}


// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
