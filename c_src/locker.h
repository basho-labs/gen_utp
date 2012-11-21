#ifndef GEN_UTP_LOCKER_H
#define GEN_UTP_LOCKER_H

// -------------------------------------------------------------------
//
// locker.h: Erlang driver mutex lock/unlock
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

#include "erl_driver.h"

class Locker {
public:
    explicit Locker(ErlDrvMutex* mutex) : mtx(mutex) {
        erl_drv_mutex_lock(mtx);
    }
    ~Locker() {
        erl_drv_mutex_unlock(mtx);
    }

private:
    ErlDrvMutex* mtx;

    Locker(const Locker&);
    void operator=(const Locker&);
};


#endif
