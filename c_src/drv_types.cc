// -------------------------------------------------------------------
//
// drv_types.h: wrap Erlang driver types for uTP driver
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

#include "drv_types.h"


using namespace UtpDrv;

UtpDrv::Binary::Binary() : bin(0)
{}

UtpDrv::Binary::Binary(const Binary& b)
{
    if (b.bin != 0) {
        bin = b.bin;
        driver_binary_inc_refc(bin);
    } else {
        bin = 0;
    }
}

UtpDrv::Binary::Binary(ErlDrvBinary* b) : bin(b)
{
}

UtpDrv::Binary::~Binary()
{
    reset();
}

UtpDrv::Binary&
UtpDrv::Binary::operator=(const Binary& b)
{
    if (&b != this) {
        reset();
        if (b.bin != 0) {
            bin = b.bin;
            driver_binary_inc_refc(bin);
        }
    }
    return *this;
}

void
UtpDrv::Binary::alloc(size_t size)
{
    reset();
    bin = driver_alloc_binary(size);
}

void
UtpDrv::Binary::reset(ErlDrvBinary* b)
{
    if (bin != 0) {
        driver_free_binary(bin);
    }
    bin = b;
}

void
UtpDrv::Binary::swap(Binary& b)
{
    ErlDrvBinary* tmp = b.bin;
    b.bin = bin;
    bin = tmp;
}

long
UtpDrv::Binary::decode(EiDecoder& decoder, size_t size)
{
    long sz;
    alloc(size);
    decoder.binary(bin->orig_bytes, sz);
    return sz;
}

const char*
UtpDrv::Binary::data() const
{
    return bin != 0 ? bin->orig_bytes : 0;
}

size_t
UtpDrv::Binary::size() const
{
    return bin != 0 ? bin->orig_size : 0;
}

bool
UtpDrv::Binary::operator==(const Binary& b) const
{
    if (b.bin == 0) {
        return bin == 0;
    }
    if (bin == 0) {
        return false;
    }
    if (b.bin->orig_size != bin->orig_size) {
        return false;
    }
    return memcmp(b.bin->orig_bytes, bin->orig_bytes, bin->orig_size) == 0;
}

UtpDrv::Binary::operator ErlDrvTermData() const
{
    return reinterpret_cast<ErlDrvTermData>(bin != 0 ? bin->orig_bytes : 0);
}

UtpDrv::Binary::operator bool() const
{
    return bin != 0;
}
