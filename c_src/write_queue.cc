// -------------------------------------------------------------------
//
// write_queue.cc: queue for uTP write data
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

#include "write_queue.h"


using namespace UtpDrv;

UtpDrv::WriteQueue::WriteQueue() : sz(0), offset(0)
{
}

UtpDrv::WriteQueue::WriteQueue(const WriteQueue& wq) :
    sz(wq.sz), offset(wq.offset)
{
    BinaryQueue::const_iterator it = wq.queue.begin();
    while (it != wq.queue.end()) {
        driver_binary_inc_refc(*it);
        queue.push_back(*it++);
    }
}

UtpDrv::WriteQueue::~WriteQueue()
{
    BinaryQueue::iterator it = queue.begin();
    while (it != queue.end()) {
        driver_free_binary(*it++);
    }
}

void
UtpDrv::WriteQueue::push_back(ErlDrvBinary* bin)
{
    queue.push_back(bin);
    sz += bin->orig_size;
}

void
UtpDrv::WriteQueue::pop_bytes(void* buf, size_t count)
{
    char* to = reinterpret_cast<char*>(buf);
    while (count > 0) {
        ErlDrvBinary* bin = queue.front();
        ErlDrvSizeT bsize = bin->orig_size;
        if (bsize - offset > count) {
            memcpy(to, bin->orig_bytes+offset, count);
            offset += count;
            sz -= count;
            break;
        } else {
            size_t to_copy = bsize - offset;
            memcpy(to, bin->orig_bytes+offset, to_copy);
            queue.pop_front();
            driver_free_binary(bin);
            offset = 0;
            count -= to_copy;
            sz -= to_copy;
            to += to_copy;
        }
    }
}

void
UtpDrv::WriteQueue::clear()
{
    queue.clear();
    sz = offset = 0;
}
