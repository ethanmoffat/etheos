
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef FWD_EOCLIENT_HPP_INCLUDED
#define FWD_EOCLIENT_HPP_INCLUDED

#include "../util/context.hpp"

class EOClient;
class ActionQueue;

struct ActionQueue_Action;
struct QueuedPacket;

// Defined in eoclient.hpp; declared here so an AsyncOperation on an EOClient can't silently use the generic policy
template<> struct util::AsyncContext<EOClient>;

template<typename TState, typename TResult = int>
using ClientAsyncOperation = AsyncOperation<EOClient, TState, TResult>;

#endif // FWD_EOCLIENT_HPP_INCLUDED
