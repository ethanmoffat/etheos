
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"

#include <eolib/protocol/net/client/packets.hpp>

namespace Handlers
{

// Player sending an emote
void Emote_Report(Character* character, const client::EmoteReportClientPacket& packet)
{
	const Emote emote = packet.emote;

	if ((emote >= Emote::Happy && emote <= Emote::Embarrassed) || (emote == Emote::Drunk && character->IsDrunk()) || emote == Emote::Playful)
	{
		character->Emote(emote, false);
	}
}

PACKET_HANDLER_REGISTER(PACKET_EMOTE)
	Register(Emote_Report, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_EMOTE)

}
