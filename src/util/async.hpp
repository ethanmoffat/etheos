
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include <functional>

#include "threadpool.hpp"
#include "../eoclient.hpp"

template<typename TState, typename TResult = int>
class AsyncOperation
{
public:
    static AsyncOperation<TState, TResult>* FromResult(TResult result, EOClient* client, TResult successCode = 0)
    {
        return new AsyncOperation(result, client, successCode);
    }

    AsyncOperation(EOClient* client, std::function<TResult(std::shared_ptr<TState>)> operation, TResult successCode = 0)
        : _client(client), _operation(operation), _successCode(successCode), _result(successCode) { }

    AsyncOperation* OnSuccess(std::function<void(EOClient*)> successCallback)
    {
        this->_successCallbacks.push_back(successCallback);
        return this;
    }

    AsyncOperation* OnFailure(std::function<void(EOClient*, TResult)> failureCallback)
    {
        this->_failureCallbacks.push_back(failureCallback);
        return this;
    }

    AsyncOperation* OnComplete(std::function<void(void)> callback)
    {
        this->_completeCallbacks.push_back(callback);
        return this;
    }

    void Execute(const std::shared_ptr<TState>& state);

    virtual ~AsyncOperation()
    {
        this->_successCallbacks.clear();
        this->_failureCallbacks.clear();
        this->_completeCallbacks.clear();
    }

private:
    AsyncOperation(TResult result, EOClient* client, TResult successCode = 0)
        : _client(client), _operation(nullptr), _successCode(successCode), _result(result) { }

    EOClient* _client;
    std::function<TResult(std::shared_ptr<TState>)> _operation;
    TResult _successCode, _result;

    std::list<std::function<void(EOClient*)>> _successCallbacks;
    std::list<std::function<void(EOClient*, TResult)>> _failureCallbacks;
    std::list<std::function<void(void)>> _completeCallbacks;
};

template<typename TState, typename TResult>
void AsyncOperation<TState, TResult>::Execute(const std::shared_ptr<TState>& state)
{
    if (this->_client->IsAsyncOpPending())
    {
        throw std::runtime_error("Client attempted to do something asynchronously but is already running an async operation");
    }

    this->_client->AsyncOpPending(true);

    auto workerProc = [this, state](const void*)
    {
        try
        {
            this->_result = this->_operation
                ? this->_operation(state)
                : this->_result;

            if (this->_result == this->_successCode)
            {
                for (auto& cb : this->_successCallbacks)
                    cb(this->_client);
            }
            else
            {
                for (auto& cb : this->_failureCallbacks)
                    cb(this->_client, this->_result);
            }

            this->_client->AsyncOpPending(false);
            for (auto& cb : this->_completeCallbacks)
                cb();

            // why `delete this`?
            //
            // AsyncOperation *must* be allocated with new in order for the long-running operation to keep going even after the calling
            // context goes out of scope. In order to clean up these objects, we either have to rely on the caller to manually delete the
            // object that is returned, or we can clean it up automatically here. Alternatively we could introduce a GC-like mechanism that
            // periodically audits any AsyncOperation objects to see if they're still running and then deletes them, but that introduces
            // tight coupling with AsyncOperation to other classes (World probably).
            //
            // Two assumptions:
            // 1. The caller does not use AsyncOperation after it has completed its work (unlikely anyway)
            // 2. The creator of AsyncOperation uses new to allocate it so delete doesn't corrupt the stack (hopefully they see this comment)
            //
            // This is very dangerous and I still don't like it but I think it's the best option available for dealing with how to clean
            // up the memory. At this point the operation is done and there shouldn't be a reference to it anymore anyway so I think it should be fine.
            //
            delete this;
        }
        catch (std::exception&)
        {
            this->_client->AsyncOpPending(false);
            for (auto& cb : this->_completeCallbacks)
                cb();

            delete this;
            throw;
        }
    };

    // If there is an operation, queue it on the threadpool
    // There may not be an operation if the result is already known, in that case
    //   invoke the procedure synchronously so the callbacks still get called
    if (this->_operation != nullptr)
        util::ThreadPool::Queue(workerProc, nullptr);
    else
        workerProc(nullptr);
}
