#ifndef UTPDRV_UTILS_H
#define UTPDRV_UTILS_H

// -------------------------------------------------------------------
//
// utils.h: utilities for uTP driver
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

#include <exception>
#include "erl_driver.h"
#include "coder.h"


namespace UtpDrv {

class SocketHandler;

extern ErlDrvSSizeT
encode_atom(char** rbuf, ErlDrvSizeT rlen, const char* atom);

extern ErlDrvSSizeT
encode_error(char** rbuf, ErlDrvSizeT rlen, const char* error);

extern ErlDrvSSizeT
encode_error(char** rbuf, ErlDrvSizeT rlen, int error);

extern void
send_not_connected(ErlDrvPort port);

// A static instance of the following class is created up front to hold the
// binary form of the {error, enomem} Erlang term. The binary is then used
// as a return value from the driver if an out-of-memory condition arises.
class NoMemError
{
public:
    NoMemError();
    ~NoMemError();

    const void* buffer() const;
    size_t size() const;

private:
    ErlDrvBinary* bin;

    // prevent copies
    NoMemError(const NoMemError&);
    void operator=(const NoMemError&);
};

extern const NoMemError enomem_error;

}


// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
