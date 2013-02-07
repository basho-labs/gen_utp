#ifndef UTPDRV_WRITE_QUEUE_H
#define UTPDRV_WRITE_QUEUE_H

// -------------------------------------------------------------------
//
// write_queue.h: queue for uTP write data
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

#include <list>
#include "erl_driver.h"


namespace UtpDrv {

class WriteQueue
{
public:
    WriteQueue();
    WriteQueue(const WriteQueue&);
    ~WriteQueue();

    void push_back(ErlDrvBinary* bin);
    void pop_bytes(void* to, size_t count);

    size_t size() const { return sz; }

    void clear();

private:
    typedef std::list<ErlDrvBinary*> BinaryQueue;
    BinaryQueue queue;
    size_t sz, offset;
};

}



// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
