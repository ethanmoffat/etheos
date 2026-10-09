
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"

#include <eolib/protocol/net/client/packets.hpp>

namespace Handlers
{

// User requesting data of all objects in their location
void Refresh_Request(Character* character, const client::RefreshRequestClientPacket&)
{
	character->Refresh();
}

PACKET_HANDLER_REGISTER(Refresh)
	Register(Refresh_Request, Playing);
PACKET_HANDLER_REGISTER_END(Refresh)

}
