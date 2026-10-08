/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include <functional>
#include <mutex>
#include <vector>

namespace util
{
    class Dispatcher
    {
    public:
        // Runs the action immediately on the main thread; from any other thread, schedules it for the next RunPending.
        static void Invoke(std::function<void()> action);

        // Run all scheduled actions on the calling thread. The main loop calls this once per tick.
        // Actions scheduled from other threads while these run wait for the next call.
        static void RunPending();

    private:
        static std::mutex _pendingLock;
        static std::vector<std::function<void()>> _pending;
    };
}
