/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "dispatcher.hpp"

#include "affinity.hpp"

#include <utility>

namespace util
{
    std::mutex Dispatcher::_pendingLock;
    std::vector<std::function<void()>> Dispatcher::_pending;

    void Dispatcher::Invoke(std::function<void()> action)
    {
        if (MainThreadAffinity::IsMainThread())
        {
            // If already on the main thread, run the action synchronously
            action();
        }
        else
        {
            std::lock_guard<std::mutex> guard(_pendingLock);
            _pending.push_back(std::move(action));
        }
    }

    void Dispatcher::RunPending()
    {
        MainThreadAffinity::ThrowOnCrossThreadAccess();

        std::vector<std::function<void()>> actions;

        {
            std::lock_guard<std::mutex> guard(_pendingLock);
            actions.swap(_pending);
        }

        for (auto& action : actions)
            action();
    }
}
