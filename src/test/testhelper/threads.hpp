#pragma once

#include <exception>
#include <functional>
#include <thread>

// Returns what the action threw on a thread other than the main thread
static std::exception_ptr RunOffMainThread(const std::function<void()>& action)
{
    std::exception_ptr thrown;
    std::thread([&action, &thrown]()
    {
        try
        {
            action();
        }
        catch (...)
        {
            thrown = std::current_exception();
        }
    }).join();

    return thrown;
}
