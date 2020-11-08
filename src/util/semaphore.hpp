
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
    Semaphore(size_t initialCount, size_t maxCount = static_cast<size_t>(-1))
        : _count(initialCount)
        , _maxCount(maxCount) { }

    Semaphore(const Semaphore&) = delete;
    Semaphore(Semaphore&& other) = delete;

    void Wait();

    template <class _Rep = long long, class _Period = std::milli>
    bool Wait(std::chrono::duration<_Rep, _Period> timeout);

    void Release(size_t count = 1);
    void Reset(size_t count, size_t maxCount = static_cast<size_t>(-1));

    size_t Count() const { return this->_count; }
    size_t MaxCount() const { return this->_maxCount; }

private:
    size_t _count;
    size_t _maxCount;
    std::condition_variable _event;
    std::mutex _mut;
};

template <class _Rep, class _Period>
bool Semaphore::Wait(std::chrono::duration<_Rep, _Period> timeout)
{
    std::unique_lock<std::mutex> lock(this->_mut);
    if (!this->_event.wait_for(lock, timeout, [&]() { return _count > 0; }))
    {
        return false;
    }

    --this->_count;

    return true;
}

}
