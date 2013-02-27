#ifndef UTPDRV_CODER_H
#define UTPDRV_CODER_H

// -------------------------------------------------------------------
//
// coder.h: ei encoder and decoder
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

#include <cstddef>
#include <exception>
#include <string>
#include "erl_driver.h"
#include "ei.h"


namespace UtpDrv {

struct EiError : public std::exception {};
struct EiBufferTooSmall : public EiError {};

class EiFun;

class EiEncoder : public ei_x_buff
{
public:
    EiEncoder();
    ~EiEncoder();

    EiEncoder& tuple_header(int arity);
    EiEncoder& list_header(int arity);
    EiEncoder& empty_list();
    EiEncoder& atom(const char* a);
    EiEncoder& atom(const char* a, int len);
    EiEncoder& atom(const std::string& s);
    EiEncoder& atom(const std::string& s, int len);
    EiEncoder& string(const char* str);
    EiEncoder& string(const char* str, int len);
    EiEncoder& string(const std::string& str);
    EiEncoder& string(const std::string& str, int len);
    EiEncoder& longval(long val);
    EiEncoder& ulongval(unsigned long val);
    EiEncoder& longlongval(long long val);
    EiEncoder& ulonglongval(unsigned long long val);
    EiEncoder& doubleval(double val);
    EiEncoder& boolval(bool val);
    EiEncoder& charval(char val);
    EiEncoder& binary(const void* buf, long len);
    EiEncoder& pid(const erlang_pid& p);
    EiEncoder& fun(const EiFun& f);
    EiEncoder& port(const erlang_port& p);
    EiEncoder& ref(const erlang_ref& r);
    EiEncoder& append(const EiEncoder&);
    EiEncoder& append(const char* buf, int len);

    const char* buffer(int& len) const;

    ErlDrvSSizeT
    copy_to_binary(ErlDrvBinary** binptr, ErlDrvSizeT rlen) const;

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
    EiDecoder& list_header(int& arity);
    EiDecoder& atom(char* val);
    EiDecoder& atom(std::string& val);
    EiDecoder& string(char* str);
    EiDecoder& string(std::string& str);
    EiDecoder& longval(long& val);
    EiDecoder& ulongval(unsigned long& val);
    EiDecoder& longlongval(long long& val);
    EiDecoder& ulonglongval(unsigned long long& val);
    EiDecoder& doubleval(double& val);
    EiDecoder& boolval(bool& val);
    EiDecoder& charval(char& val);
    EiDecoder& binary(char* bin, long& size);
    EiDecoder& pid(erlang_pid& p);
    EiDecoder& fun(EiFun& f);
    EiDecoder& port(erlang_port& p);
    EiDecoder& ref(erlang_ref& r);
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

class EiFun : public erlang_fun
{
public:
    EiFun() : allocated(false) {}
    ~EiFun() {
        if (allocated) {
            free_fun(this);
        }
    }

    friend class EiDecoder;

private:
    bool allocated;

    // prevent copies
    EiFun(const EiFun&);
    void operator=(const EiFun&);
};

}


// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
