
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../converters.hpp"
#include "../eoclient.hpp"
#include "../player.hpp"
#include "../world.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <stdexcept>
#include <utility>

namespace Handlers
{

static bool IsPendingWarp(Character* character, int map_id, int session_id)
{
	if (character->warp_effect == Character::NoPendingWarp)
	{
		return false;
	}

	if (!character->world->CheckSession(character->warp_session_id, session_id)
	 || !character->world->CheckSession(character->mapid, map_id))
	{
		character->player->client->Close();
		return false;
	}

	return true;
}

// Player accepting a warp request from the server
void Warp_Accept(Character* character, const client::WarpAcceptClientPacket& packet)
{
	if (!IsPendingWarp(character, packet.map_id, packet.session_id))
	{
		return;
	}

	// The original server always sends MapSwitch, which is the only case that carries the warp effect
	server::WarpAgreeServerPacket::WarpTypeDataMapSwitch map_switch;
	map_switch.map_id = character->mapid;
	map_switch.warp_effect = character->warp_effect;

	server::WarpAgreeServerPacket reply = server::WarpAgreeServerPacket::ForMapSwitch(std::move(map_switch));
	reply.nearby = Converters::NearbyInfoFrom(character);

	character->warp_effect = Character::NoPendingWarp;
	character->Send(reply);
}

// Player needs a copy of the map they're being warped to
void Warp_Take(Character* character, const client::WarpTakeClientPacket& packet)
{
	if (!IsPendingWarp(character, packet.map_id, packet.session_id))
	{
		return;
	}

	if (!character->player->client->UploadMap(character->mapid, server::InitReply::WarpMap))
		throw std::runtime_error("Failed to upload file");
}

PACKET_HANDLER_REGISTER(PACKET_WARP)
	Register(Warp_Accept, Playing);
	Register(Warp_Take, Playing, 0.15);
PACKET_HANDLER_REGISTER_END(PACKET_WARP)

}
