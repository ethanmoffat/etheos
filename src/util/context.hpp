/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>

template<typename TContext, typename TState, typename TResult = int>
class AsyncOperation;

namespace util
{
    using AsyncKey = std::uint64_t;

    /**
     * Policy for the context type of an AsyncOperation. Specialize it to give a context type a key (only one operation
     * per key runs at a time) and error handling for exceptions thrown by the operation or its callbacks.
     */
    template<typename TContext>
    struct AsyncContext
    {
        static std::optional<AsyncKey> Key(const TContext&) { return std::nullopt; }
        static void OnError(TContext&, const std::string&) { }
    };

    // Keys of the async operations that are running. Only used on the main thread: Execute and the dispatched completion.
    class AsyncKeys
    {
    public:
        using Key = AsyncKey;

        static bool IsPending(Key key) { return keys.count(key) != 0; }

    private:
        template<typename, typename, typename> friend class ::AsyncOperation;

        inline static std::unordered_set<Key> keys;
    };
}
