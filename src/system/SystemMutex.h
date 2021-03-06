/*
 *
 *    Copyright (c) 2016-2017 Nest Labs, Inc.
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

/**
 *    @file
 *      This file declares the abstraction of mutual exclusion locks
 *      offered by the target platform.
 */

#ifndef SYSTEMMUTEX_H
#define SYSTEMMUTEX_H

// Include configuration headers
#include <SystemLayer/SystemConfig.h>

// Include dependent headers
#include <SystemLayer/SystemError.h>

#include <Weave/Support/NLDLLUtil.h>

#if !WEAVE_SYSTEM_CONFIG_NO_LOCKING

#if WEAVE_SYSTEM_CONFIG_POSIX_LOCKING
#include <pthread.h>
#endif // WEAVE_SYSTEM_CONFIG_POSIX_LOCKING

#if WEAVE_SYSTEM_CONFIG_FREERTOS_LOCKING
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#endif // WEAVE_SYSTEM_CONFIG_FREERTOS_LOCKING

namespace nl {
namespace Weave {
namespace System {

/**
 *  @class Mutex
 *
 *  @brief
 *      This class represents a simple mutual exclusion lock used on platforms with preemptively scheduled multi-threaded
 *      programming environments, for example, POSIX threads and FreeRTOS. The lock is non-recursive, and may not be used in a hardware
 *      interrupt context. The constructor and destructor are defined as null functions to facilitate using objects with \c static
 *      storage duration and uninitialized memory. Use \c Init method to initialize. The copy/move operators are not provided.
 *
 */
class NL_DLL_EXPORT Mutex
{
public:
    Mutex(void);
    ~Mutex(void);

    static Error Init(Mutex& aMutex);

    void Lock(void);    /**< Acquire the mutual exclusion lock, blocking the current thread indefinitely if necessary. */
    void Unlock(void);  /**< Release the mutual exclusion lock (can block on some systems until scheduler completes). */

private:
#if WEAVE_SYSTEM_CONFIG_POSIX_LOCKING
    pthread_mutex_t mPOSIXMutex;
#endif // WEAVE_SYSTEM_CONFIG_POSIX_LOCKING

#if WEAVE_SYSTEM_CONFIG_FREERTOS_LOCKING
#if (configSUPPORT_STATIC_ALLOCATION == 1)
    StaticSemaphore_t mFreeRTOSSemaphoreObj;
#endif // (configSUPPORT_STATIC_ALLOCATION == 1)
    volatile SemaphoreHandle_t mFreeRTOSSemaphore;
    volatile int mInitialized;
#endif // WEAVE_SYSTEM_CONFIG_FREERTOS_LOCKING

    Mutex(const Mutex&)             /* = delete */;
    Mutex& operator =(const Mutex&) /* = delete */;
};

inline Mutex::Mutex(void)
{
}

inline Mutex::~Mutex(void)
{
}

#if WEAVE_SYSTEM_CONFIG_POSIX_LOCKING
inline void Mutex::Lock(void)
{
    pthread_mutex_lock(&this->mPOSIXMutex);
}

inline void Mutex::Unlock(void)
{
    pthread_mutex_unlock(&this->mPOSIXMutex);
}
#endif // WEAVE_SYSTEM_CONFIG_POSIX_LOCKING

#if WEAVE_SYSTEM_CONFIG_FREERTOS_LOCKING
inline void Mutex::Unlock(void)
{
    xSemaphoreGive(this->mFreeRTOSSemaphore);
}
#endif // WEAVE_SYSTEM_CONFIG_FREERTOS_LOCKING

} // namespace System
} // namespace Weave
} // namespace nl

#endif // !WEAVE_SYSTEM_CONFIG_NO_LOCKING

#endif // defined(SYSTEMMUTEX_H)
