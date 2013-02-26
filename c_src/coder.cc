// -------------------------------------------------------------------
//
// coder.cc: ei encoder and decoder
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

#include <stdexcept>
#include "coder.h"


using namespace UtpDrv;

UtpDrv::EiEncoder::EiEncoder()
{
    if (ei_x_new_with_version(this) != 0) {
        throw EiError();
    }
}

UtpDrv::EiEncoder::~EiEncoder()
{
    ei_x_free(this);
}

EiEncoder&
UtpDrv::EiEncoder::tuple_header(int arity)
{
    if (ei_x_encode_tuple_header(this, arity) != 0) {
        throw EiError();
    }
    return *this;
}

EiEncoder&
UtpDrv::EiEncoder::list_header(int arity)
{
    if (ei_x_encode_list_header(this, arity) != 0) {
        throw EiError();
    }
    return *this;
}

EiEncoder&
UtpDrv::EiEncoder::empty_list()
{
    if (ei_x_encode_empty_list(this) != 0) {
        throw EiError();
    }
    return *this;
}

EiEncoder&
UtpDrv::EiEncoder::atom(const char* a)
{
    if (ei_x_encode_atom(this, a) != 0) {
        throw EiError();
    }
    return *this;
}

EiEncoder&
UtpDrv::EiEncoder::string(const char* str)
{
    if (ei_x_encode_string(this, str) != 0) {
        throw EiError();
    }
    return *this;
}

EiEncoder&
UtpDrv::EiEncoder::ulongval(unsigned long val)
{
    if (ei_x_encode_ulong(this, val) != 0) {
        throw EiError();
    }
    return *this;
}

EiEncoder&
UtpDrv::EiEncoder::longval(long val)
{
    if (ei_x_encode_long(this, val) != 0) {
        throw EiError();
    }
    return *this;
}

EiEncoder&
UtpDrv::EiEncoder::binary(const void* buf, long len)
{
    if (ei_x_encode_binary(this, buf, len) != 0) {
        throw EiError();
    }
    return *this;
}

EiEncoder&
UtpDrv::EiEncoder::append_buf(const char* buf, int len)
{
    if (ei_x_append_buf(this, buf, len) != 0) {
        throw EiError();
    }
    return *this;
}

const char*
UtpDrv::EiEncoder::buffer(int& len) const
{
    len = index+1;
    return buff;
}

ErlDrvSSizeT
UtpDrv::EiEncoder::copy_to_binary(ErlDrvBinary** binp, ErlDrvSizeT rlen) const
{
    ErlDrvSSizeT size = index+1;
    if (size > ErlDrvSSizeT(rlen)) {
        // We do not free *binp here because we assume the pointer-to-binary
        // passed in follows the rules of the rbuf argument to the Erlang
        // driver control and call entry point functions. If we reallocate
        // the binary as below, the Erlang runtime takes care of freeing it.
        *binp = driver_alloc_binary(size);
        if (*binp == 0) {
            throw std::bad_alloc();
        }
        memcpy((*binp)->orig_bytes, buff, size);
    } else {
        char** p = reinterpret_cast<char**>(binp);
        memcpy(*p, buff, size);
    }
    return size;
}

//--------------------------------------------------------------------

UtpDrv::EiDecoder::EiDecoder(const char* bf, int ln) :
    buf(bf), len(ln), index(0)
{
    int vsn;
    if (ei_decode_version(buf, &index, &vsn) != 0) {
        throw EiError();
    }
}

UtpDrv::EiDecoder::~EiDecoder()
{}

EiDecoder&
UtpDrv::EiDecoder::tuple_header(int& arity)
{
    if (ei_decode_tuple_header(buf, &index, &arity) != 0) {
        throw EiError();
    }
    return *this;
}

EiDecoder&
UtpDrv::EiDecoder::string(char* str)
{
    if (ei_decode_string(buf, &index, str) != 0) {
        throw EiError();
    }
    return *this;
}

EiDecoder&
UtpDrv::EiDecoder::ulong(unsigned long& val)
{
    if (ei_decode_ulong(buf, &index, &val) != 0) {
        throw EiError();
    }
    return *this;
}

EiDecoder&
UtpDrv::EiDecoder::binary(char* bin, long& size)
{
    if (ei_decode_binary(buf, &index, bin, &size) != 0) {
        throw EiError();
    }
    return *this;
}

EiDecoder&
UtpDrv::EiDecoder::skip()
{
    if (ei_skip_term(buf, &index) != 0) {
        throw EiError();
    }
    return *this;
}

EiDecoder&
UtpDrv::EiDecoder::type(int& type, int& size)
{
    if (ei_get_type(buf, &index, &type, &size) != 0) {
        throw EiError();
    }
    return *this;
}
