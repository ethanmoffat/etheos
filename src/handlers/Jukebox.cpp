
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../eodata.hpp"
#include "../map.hpp"
#include "../timer.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>

namespace Handlers
{

// Opened the jukebox listing
void Jukebox_Open(Character* character, const client::JukeboxOpenClientPacket& packet)
{
	const unsigned char x = packet.coords.x;
	const unsigned char y = packet.coords.y;

	if (!character->InRange(x, y)
	 || character->map->GetSpec(x, y) != emf::MapTileSpec::Jukebox)
	{
		return;
	}

	server::JukeboxOpenServerPacket reply;
	reply.map_id = character->mapid;

	if (character->map->jukebox_protect > Timer::GetTime())
	{
		reply.jukebox_player = character->map->jukebox_player;
	}

	character->jukebox_open = true;

	character->Send(reply);
}

// Requested a song
void Jukebox_Msg(Character* character, const client::JukeboxMsgClientPacket& packet)
{
	if (character->trading) return;

	const int track = packet.track_id;

	if (!character->jukebox_open
	 || character->map->jukebox_protect > Timer::GetTime()
	 || (track < 0 || track > static_cast<int>(character->world->config["JukeboxSongs"]))
	 || character->HasItem(1) < static_cast<int>(character->world->config["JukeboxPrice"]))
	{
		character->Send(server::JukeboxReplyServerPacket());
		return;
	}

	character->DelItem(1, static_cast<int>(character->world->config["JukeboxPrice"]));

	character->map->jukebox_player = character->SourceName();
	character->map->jukebox_protect = Timer::GetTime() + static_cast<int>(character->world->config["JukeboxTimer"]);

	server::JukeboxAgreeServerPacket reply;
	reply.gold_amount = character->HasItem(1);
	character->Send(reply);

	server::JukeboxUseServerPacket use;
	use.track_id = track + 1;
	character->map->SendToMap(use);
}

// Bard skill music
void Jukebox_Use(Character* character, const client::JukeboxUseClientPacket& packet)
{
	const auto& eif = character->world->eif;
	const auto& esf = character->world->esf;

	// Check if user has the bard skill
	auto find_it = find_if(UTIL_RANGE(character->spells),
		[&](const eolib::protocol::net::Spell& spell) { return esf->Get(spell.id).type == pub::SkillType::Bard; });

	if (find_it == character->spells.end())
		return;

	// Check if the held weapon is an instrument
	int instrument = AsEquipment(eif->Get(character->paperdoll[Character::Weapon])).doll_graphic;

	if (!character->world->IsInstrument(instrument))
		return;

	character->PlayBard(instrument, packet.note_id, false);
}

PACKET_HANDLER_REGISTER(PACKET_JUKEBOX)
	Register(Jukebox_Open, Playing);
	Register(Jukebox_Msg, Playing);
	Register(Jukebox_Use, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_JUKEBOX)

}
