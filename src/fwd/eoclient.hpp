
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef FWD_EOCLIENT_HPP_INCLUDED
#define FWD_EOCLIENT_HPP_INCLUDED

class EOClient;
class ActionQueue;

struct ActionQueue_Action;
struct QueuedPacket;

enum InitReply : unsigned char
{
	INIT_PLAYERS = 8,
	INIT_FRIEND_LIST_PLAYERS = 10
};

#endif // FWD_EOCLIENT_HPP_INCLUDED
