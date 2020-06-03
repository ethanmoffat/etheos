
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include <condition_variable>

namespace util
{

class Semaphore
{
public:
    Semaphore(size_t initialCount) : _count(initialCount) { }

    Semaphore(const Semaphore&) = delete;
    Semaphore(Semaphore&& other) = delete;

    void Wait();

    template <class _Rep = __int64, class _Period = std::milli>
    bool Wait(std::chrono::duration<_Rep, _Period> timeout);

    void Release(size_t count = 1);

private:
    size_t _count;
    std::condition_variable _event;
    std::mutex _mut;
};

}
