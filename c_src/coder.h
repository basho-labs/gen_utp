#ifndef UTPDRV_CODER_H
#define UTPDRV_CODER_H

// -------------------------------------------------------------------
//
// coder.h: ei encoder and decoder
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

#include <cstddef>
#include <exception>
#include "erl_driver.h"
#include "ei.h"


namespace UtpDrv {

struct EiError : public std::exception {};
struct EiBufferTooSmall : public EiError {};

class EiEncoder : public ei_x_buff
{
public:
    EiEncoder();
    ~EiEncoder();

    EiEncoder& tuple_header(int arity);
    EiEncoder& atom(const char* a);
    EiEncoder& string(const char* str);
    EiEncoder& ulong(unsigned long val);

    const char* buffer(int& len) const;

    ErlDrvBinary* copy_to_binary(ErlDrvSSizeT& size) const;

private:
    // prevent copies
    EiEncoder(const EiEncoder&);
    void operator=(const EiEncoder&);
};

class EiDecoder
{
public:
    EiDecoder(const char* buf, int len);
    ~EiDecoder();

    EiDecoder& tuple_header(int& arity);
    EiDecoder& string(char* str);
    EiDecoder& ulong(unsigned long& val);
    EiDecoder& binary(char* bin, long& size);
    EiDecoder& skip();
    EiDecoder& type(int& type, int& size);

private:
    const char* buf;
    const int len;
    int index;

    // prevent copies
    EiDecoder(const EiDecoder&);
    void operator=(const EiDecoder&);
};

}


// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
