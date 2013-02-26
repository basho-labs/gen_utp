#ifndef UTPDRV_DRV_TYPES_H
#define UTPDRV_DRV_TYPES_H

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

#include <string>
#include "erl_driver.h"
#include "coder.h"


namespace UtpDrv {

class Binary
{
public:
    Binary();
    Binary(const Binary&);
    explicit Binary(ErlDrvBinary* b);
    ~Binary();

    Binary& operator=(const Binary&);

    void alloc(size_t size);
    void reset(ErlDrvBinary* b = 0);
    void swap(Binary&);

    long decode(EiDecoder& decoder, size_t size);

    const char* data() const;
    size_t size() const;

    bool operator==(const Binary&) const;

    operator ErlDrvTermData() const;
    operator bool() const;

private:
    ErlDrvBinary* bin;
};

typedef std::basic_string<unsigned char> ustring;

}



// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
