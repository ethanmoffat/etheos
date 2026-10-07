
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../converters.hpp"
#include "../map.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

namespace Handlers
{

// User requests another's Book (Quest history)
void Book_Request(Character* character, const client::BookRequestClientPacket& packet)
{
	Character* target = character->map->GetCharacterPID(packet.player_id);

	if (!target)
	{
		target = character;
	}

	server::BookReplyServerPacket reply;
	reply.details = Converters::CharacterDetailsFrom(target);
	reply.icon = Converters::CharacterIconFrom(target);
	reply.quest_names = Converters::FinishedQuestNamesFrom(target);
	character->Send(reply);
}

PACKET_HANDLER_REGISTER(PACKET_BOOK)
	Register(Book_Request, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_BOOK)

}
