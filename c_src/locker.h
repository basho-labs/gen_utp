#ifndef GEN_UTP_LOCKER_H
#define GEN_UTP_LOCKER_H

// -------------------------------------------------------------------
//
// locker.h: Erlang driver lock/unlock
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

#include "erl_driver.h"

namespace UtpDrv {

template<typename T, void (*lock)(T), void (*unlock)(T)>
class BaseLocker
{
public:
    explicit BaseLocker(T m) : mtx(m) { lock(mtx); }
    ~BaseLocker() { unlock(mtx); }

private:
    T mtx;

    BaseLocker(const BaseLocker&);
    BaseLocker& operator=(const BaseLocker&);
};

typedef BaseLocker<ErlDrvMutex*,
                   erl_drv_mutex_lock, erl_drv_mutex_unlock> MutexLocker;

typedef BaseLocker<ErlDrvPDL,
                   driver_pdl_lock, driver_pdl_unlock> PdlLocker;

}



// this block comment is for emacs, do not delete
// Local Variables:
// mode: c++
// c-file-style: "stroustrup"
// c-file-offsets: ((innamespace . 0))
// End:

#endif
