
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "semaphore.hpp"

namespace util
{

void Semaphore::Wait()
{
    std::unique_lock<std::mutex> lock(this->_mut);
    this->_event.wait(lock, [&]() { return _count > 0; });
    --this->_count;
}

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

void Semaphore::Release(size_t count)
{
    std::unique_lock<std::mutex> lock(this->_mut);

    this->_count = this->_count + count >= this->_maxCount
        ? this->_maxCount
        : this->_count + count;

    this->_event.notify_one();
}

void Semaphore::Reset(size_t count, size_t maxCount)
{
    std::unique_lock<std::mutex> lock(this->_mut);

    this->_event.notify_all();

    this->_count = count;
    this->_maxCount = maxCount;
}

}