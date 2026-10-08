/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <thread>

#include "../platform.h"
#ifdef WIN32
void service_main(int argc, char *argv[]);
#endif

namespace util
{
    // A violation representing a function call from the incorrect thread.
    class ThreadAffinityViolation : public std::logic_error
    {
    public:
        using std::logic_error::logic_error;
    };

    // A violation representing a check for thread affinity on an affinity object that is not bound.
    class ThreadBindViolation : public std::logic_error
    {
    public:
        using std::logic_error::logic_error;
    };

    // Represents association of an object with a thread id.
    class ThreadAffinity
    {
    public:
        enum class Behavior : std::uint8_t
        {
            Default         = 0x00, // Requires a call to Bind before the first IsOwningThread() check
            LooseEvaluation = 0x01, // Any thread is the owning thread until Bind is called
            BindOnCreate    = 0x02, // Binds the affinity object immediately when created
            SingleBind      = 0x04, // Allows only a single bind call - attempting to re-bind throws
        };

        friend constexpr Behavior operator|(Behavior lhs, Behavior rhs)
        {
            return static_cast<Behavior>(static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
        }

        friend constexpr Behavior operator&(Behavior lhs, Behavior rhs)
        {
            return static_cast<Behavior>(static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
        }

    public:
        ThreadAffinity() : ThreadAffinity(Behavior::Default) { }
        explicit ThreadAffinity(Behavior behavior)
            : _behavior(behavior)
            , _owner(std::thread::id())
        {
            if (HasBehavior(Behavior::BindOnCreate))
                this->Bind();
        }

        void Bind()
        {
            if (!HasBehavior(Behavior::SingleBind))
            {
                this->_owner = std::this_thread::get_id();
                return;
            }

            std::thread::id unbound;
            if (!this->_owner.compare_exchange_strong(unbound, std::this_thread::get_id()))
            {
                throw ThreadBindViolation("second ThreadAffinity::Bind call is disallowed with SingleBind behavior");
            }
        }

        bool IsOwningThread() const
        {
            const auto owner = this->_owner.load();
            if (owner == std::thread::id()) // std::thread::id() is standardized as "no thread id"
            {
                if (HasBehavior(Behavior::LooseEvaluation))
                {
                    return true;
                }

                throw ThreadBindViolation("ThreadAffinity::IsOwningThread was called before Bind");
            }

            return owner == std::this_thread::get_id();
        }

    private:
        Behavior _behavior;
        std::atomic<std::thread::id> _owner;

        bool HasBehavior(Behavior flags) const
        {
            return (this->_behavior & flags) != Behavior::Default;
        }

// Expose a Rebind() operation on Win32
// This is required for service_main since Windows services dispatch app main to a different thread. This
//   different thread breaks MainThreadAffinity which binds to the main thread on static initialization.
// MainThreadAffinity defines a RebindKey which is only constructable from service_main (via friend declaration).
#ifdef WIN32
    private:
        friend class MainThreadAffinity;
        void ForceRebind() { this->_owner = std::this_thread::get_id(); }
#endif // WIN32
    };

    // Represents thread affinity for the main thread (static/singleton class)
    class MainThreadAffinity
    {
    public:
        static bool IsMainThread() { return _affinity.IsOwningThread(); }

        // Throws if the calling thread is not the thread invoking the 'main' function.
        // The default argument is evaluated at the call site, giving the caller's function name (std::source_location in C++20)
        static void ThrowOnCrossThreadAccess(const char* callSite = __builtin_FUNCTION())
        {
            if (!MainThreadAffinity::IsMainThread())
            {
                throw ThreadAffinityViolation(std::string(callSite) + " was called from a thread other than the main thread");
            }
        }

    private:
        using Behavior = ThreadAffinity::Behavior;
        inline static ThreadAffinity _affinity{Behavior::BindOnCreate | Behavior::SingleBind};

#ifdef WIN32
    public:
        class RebindKey
        {
        private:
            RebindKey() {}
            friend void ::service_main(int argc, char *argv[]);
        };

        static void Rebind(RebindKey) { _affinity.ForceRebind(); }
#endif // WIN32
    };
}
