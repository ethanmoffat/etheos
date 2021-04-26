#include "async.hpp"
#include "../eoclient.hpp"
#include "threadpool.hpp"

AsyncOperation::AsyncOperation(EOClient* client, std::function<int(void*)> operation, int successCode)
    : _client(client), _operation(operation), _successCode(successCode)
{
    this->_successCallback = [](EOClient *) { };
    this->_failureCallback = [](EOClient *) { };
    this->_completeCallback = []() { };
}

AsyncOperation* AsyncOperation::OnSuccess(std::function<void(EOClient*)> successCallback)
{
    this->_successCallback = successCallback;
    return this;
}

AsyncOperation* AsyncOperation::OnFailure(std::function<void(EOClient*)> failureCallback)
{
    this->_failureCallback = failureCallback;
    return this;
}

AsyncOperation* AsyncOperation::OnComplete(std::function<void(void)> callback)
{
    this->_completeCallback = callback;
    return this;
}

void AsyncOperation::Execute(void* state)
{
    if (this->_client->IsAsyncOpPending())
    {
        throw std::runtime_error("Client attempted to do something asynchronously but is already running an async operation");
    }

    this->_client->AsyncOpPending(true);

    util::ThreadPool::Queue([this](const void* tpState)
    {
        try
        {
            auto result = this->_operation(const_cast<void*>(tpState));
            if (result == this->_successCode)
            {
                this->_successCallback(this->_client);
            }
            else
            {
                this->_failureCallback(this->_client);
            }

            this->_client->AsyncOpPending(false);
            this->_completeCallback();

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
            this->_completeCallback();
            delete this;

            throw;
        }
    }, state);
}
