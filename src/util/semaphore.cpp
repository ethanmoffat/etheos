
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

void Semaphore::Release(size_t count)
{
    std::unique_lock<std::mutex> lock(this->_mut);

    auto oldCount = this->_count;
    this->_count = this->_count + count >= this->_maxCount
        ? this->_maxCount
        : this->_count + count;

    for (size_t i = oldCount; i < this->_count; ++i)
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