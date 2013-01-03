// -------------------------------------------------------------------
//
// utils.cc: utilities for uTP driver
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

#include <unistd.h>
#include "utils.h"
#include "coder.h"
#include "globals.h"
#include "main_handler.h"
#include "utp_handler.h"


using namespace UtpDrv;

const UtpDrv::NoMemError UtpDrv::enomem_error;

ErlDrvSSizeT
UtpDrv::encode_atom(char** rbuf, ErlDrvSizeT rlen, const char* atom)
{
    EiEncoder encoder;
    try {
        encoder.atom(atom);
        ErlDrvBinary** binptr = reinterpret_cast<ErlDrvBinary**>(rbuf);
        return encoder.copy_to_binary(binptr, rlen);
    } catch (std::exception&) {
        memcpy(*rbuf, enomem_error.buffer(), enomem_error.size());
        return enomem_error.size();
    }
}

ErlDrvSSizeT
UtpDrv::encode_error(char** rbuf, ErlDrvSizeT rlen, const char* error)
{
    EiEncoder encoder;
    try {
        encoder.tuple_header(2).atom("error").atom(error);
        ErlDrvBinary** binptr = reinterpret_cast<ErlDrvBinary**>(rbuf);
        return encoder.copy_to_binary(binptr, rlen);
    } catch (std::exception&) {
        memcpy(*rbuf, enomem_error.buffer(), enomem_error.size());
        return enomem_error.size();
    }
}

ErlDrvSSizeT
UtpDrv::encode_error(char** rbuf, ErlDrvSizeT rlen, int error)
{
    return encode_error(rbuf, rlen, erl_errno_id(error));
}

ErlDrvPort
UtpDrv::create_port(ErlDrvTermData owner, SocketHandler* h)
{
    ErlDrvData port_drv_data = reinterpret_cast<ErlDrvData>(h);
    ErlDrvPort new_port = driver_create_port(MainHandler::drv_port(), owner,
                                             drv_name, port_drv_data);
    return new_port;
}

void
UtpDrv::send_not_connected(ErlDrvPort port)
{
    ErlDrvTermData caller = driver_caller(port);
    ErlDrvTermData term[] = {
        ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("utp_reply")),
        ERL_DRV_PORT, driver_mk_port(port),
        ERL_DRV_ATOM, driver_mk_atom(const_cast<char*>("error")),
        ERL_DRV_ATOM, driver_mk_atom(erl_errno_id(ENOTCONN)),
        ERL_DRV_TUPLE, 2,
        ERL_DRV_TUPLE, 3,
    };
    driver_send_term(port, caller, term, sizeof term/sizeof *term);
}

UtpDrv::NoMemError::NoMemError() : bin(0)
{
    encode_error(reinterpret_cast<char**>(&bin), 0, ENOMEM);
}

UtpDrv::NoMemError::~NoMemError()
{
    if (bin != 0) {
        driver_free_binary(bin);
    }
}

const void*
UtpDrv::NoMemError::buffer() const
{
    return bin->orig_bytes;
}

size_t
UtpDrv::NoMemError::size() const
{
    return bin->orig_size;
}
