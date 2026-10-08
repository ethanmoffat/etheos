/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include <functional>
#include <list>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "context.hpp"
#include "dispatcher.hpp"
#include "threadpool.hpp"

/**
 * Runs an operation on the thread pool, then its callbacks on the main thread (util::Dispatcher). Callbacks get the
 * context the operation was created with. util::AsyncContext<TContext> gives the context a key (an operation can't
 * start while another one with the same key is running) and its error handling.
 */
template<typename TContext, typename TState, typename TResult>
class AsyncOperation : public std::enable_shared_from_this<AsyncOperation<TContext, TState, TResult>>
{
public:
    using Key = util::AsyncKeys::Key;

    static std::shared_ptr<AsyncOperation> Create(TContext* context, std::function<TResult(std::shared_ptr<TState>)> operation, TResult successCode = TResult{})
    {
        return std::shared_ptr<AsyncOperation>(new AsyncOperation(context, std::move(operation), successCode));
    }

    static std::shared_ptr<AsyncOperation> FromResult(TResult result, TContext* context, TResult successCode = TResult{})
    {
        return std::shared_ptr<AsyncOperation>(new AsyncOperation(result, context, successCode));
    }

    std::shared_ptr<AsyncOperation> OnSuccess(std::function<void(TContext*)> successCallback)
    {
        this->_successCallbacks.push_back(successCallback);
        return this->shared_from_this();
    }

    std::shared_ptr<AsyncOperation> OnFailure(std::function<void(TContext*, TResult)> failureCallback)
    {
        this->_failureCallbacks.push_back(failureCallback);
        return this->shared_from_this();
    }

    std::shared_ptr<AsyncOperation> OnComplete(std::function<void(void)> callback)
    {
        this->_completeCallbacks.push_back(callback);
        return this->shared_from_this();
    }

    void Execute(const std::shared_ptr<TState>& state);

    virtual ~AsyncOperation()
    {
        this->_successCallbacks.clear();
        this->_failureCallbacks.clear();
        this->_completeCallbacks.clear();
    }

private:
    AsyncOperation(TContext* context, std::function<TResult(std::shared_ptr<TState>)> operation, TResult successCode)
        : _context(context), _operation(std::move(operation)), _successCode(successCode), _result(successCode) { }

    AsyncOperation(TResult result, TContext* context, TResult successCode)
        : _context(context), _operation(nullptr), _successCode(successCode), _result(result) { }

    TContext* _context;
    std::function<TResult(std::shared_ptr<TState>)> _operation;
    TResult _successCode, _result;

    std::optional<Key> _key; // set during execution via the AsyncContext<TContext> template class

    std::list<std::function<void(TContext*)>> _successCallbacks;
    std::list<std::function<void(TContext*, TResult)>> _failureCallbacks;
    std::list<std::function<void(void)>> _completeCallbacks;

    void Complete();
    void CompleteWithError(const std::string& error);
};

template<typename TContext, typename TState, typename TResult>
void AsyncOperation<TContext, TState, TResult>::Execute(const std::shared_ptr<TState>& state)
{
    util::MainThreadAffinity::ThrowOnCrossThreadAccess();

    this->_key = util::AsyncContext<TContext>::Key(*this->_context);

    if (this->_key)
    {
        if (util::AsyncKeys::IsPending(*this->_key))
        {
            throw std::runtime_error("Attempted to start an async operation while another one with the same key is running");
        }

        util::AsyncKeys::keys.insert(*this->_key);
    }

    // If there is an operation, queue it on the threadpool; finalization runs via invocation to the main thread.
    // There is no operation when the result is known (e.g. via FromResult); finalization runs synchronously.
    if (this->_operation != nullptr)
    {
        // The worker hands its reference to the main thread, so the operation is always destroyed there
        auto workerProc = [self = this->shared_from_this(), state](const void*) mutable
        {
            try
            {
                self->_result = self->_operation(state);
            }
            catch (const std::exception& e)
            {
                util::Dispatcher::Invoke(
                    [self = std::move(self), error = std::string(e.what())]()
                    {
                        self->CompleteWithError(error);
                    }
                );
                return;
            }
            catch (...)
            {
                util::Dispatcher::Invoke(
                    [self = std::move(self)]()
                    {
                        self->CompleteWithError("unknown exception");
                    }
                );
                return;
            }

            util::Dispatcher::Invoke([self = std::move(self)]() { self->Complete(); });
        };

        util::ThreadPool::Queue(std::move(workerProc), nullptr);
    }
    else
    {
        this->Complete();
    }
}

template<typename TContext, typename TState, typename TResult>
void AsyncOperation<TContext, TState, TResult>::Complete()
{
    if (this->_key)
        util::AsyncKeys::keys.erase(*this->_key);

    // The first throwing callback skips the rest of its list
    try
    {
        if (this->_result == this->_successCode)
        {
            for (auto& cb : this->_successCallbacks)
                cb(this->_context);
        }
        else
        {
            for (auto& cb : this->_failureCallbacks)
                cb(this->_context, this->_result);
        }
    }
    catch (const std::exception& e)
    {
        util::AsyncContext<TContext>::OnError(*this->_context, e.what());
    }
    catch (...)
    {
        util::AsyncContext<TContext>::OnError(*this->_context, "unknown exception");
    }

    for (auto& cb : this->_completeCallbacks)
    {
        try
        {
            cb();
        }
        catch (const std::exception& e)
        {
            util::AsyncContext<TContext>::OnError(*this->_context, e.what());
        }
        catch (...)
        {
            util::AsyncContext<TContext>::OnError(*this->_context, "unknown exception");
        }
    }
}

template<typename TContext, typename TState, typename TResult>
void AsyncOperation<TContext, TState, TResult>::CompleteWithError(const std::string& error)
{
    if (this->_key)
        util::AsyncKeys::keys.erase(*this->_key);

    util::AsyncContext<TContext>::OnError(*this->_context, error);

    for (auto& cb : this->_completeCallbacks)
    {
        try
        {
            cb();
        }
        catch (const std::exception& e)
        {
            util::AsyncContext<TContext>::OnError(*this->_context, e.what());
        }
        catch (...)
        {
            util::AsyncContext<TContext>::OnError(*this->_context, "unknown exception");
        }
    }
}
