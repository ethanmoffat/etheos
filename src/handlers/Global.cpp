
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"

#include <eolib/protocol/net/client/packets.hpp>

namespace Handlers
{

// User enabled whispers
void Global_Remove(Character* character, const client::GlobalRemoveClientPacket&)
{
	character->whispers = true;
}

// User disabled whispers
void Global_Player(Character* character, const client::GlobalPlayerClientPacket&)
{
	character->whispers = false;
}

// User has opened/closed the global tab
void Global_Open(Character*, const client::GlobalOpenClientPacket&)
{
}

// User has closed the global tab
void Global_Close(Character*, const client::GlobalCloseClientPacket&)
{
}

PACKET_HANDLER_REGISTER(Global)
	Register(Global_Remove, Playing);
	Register(Global_Player, Playing);
	Register(Global_Open, Playing);
	Register(Global_Close, Playing);
PACKET_HANDLER_REGISTER_END(Global)

}
