
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
    AsyncOperation* OnFailure(std::function<void(EOClient*)> failureCallback);
    AsyncOperation* OnComplete(std::function<void(void)> callback);

    void Execute(void* state);

private:
    EOClient* _client;
    std::function<int(void*)> _operation;
    int _successCode;

    std::function<void(EOClient*)> _successCallback;
    std::function<void(EOClient*)> _failureCallback;
    std::function<void(void)> _completeCallback;
};
