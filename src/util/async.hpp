
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include <functional>

#include "../fwd/eoclient.hpp"

class AsyncOperation
{
public:
    AsyncOperation(EOClient* client, std::function<int(void*)> operation, int successCode = 0);

    AsyncOperation* OnSuccess(std::function<void(EOClient*)> successCallback);
    AsyncOperation* OnFailure(std::function<void(EOClient*, int)> failureCallback);
    AsyncOperation* OnComplete(std::function<void(void)> callback);

    void Execute(void* state);

    virtual ~AsyncOperation();

private:
    EOClient* _client;
    std::function<int(void*)> _operation;
    int _successCode;

    std::list<std::function<void(EOClient*)>> _successCallbacks;
    std::list<std::function<void(EOClient*, int)>> _failureCallbacks;
    std::list<std::function<void(void)>> _completeCallbacks;
};
