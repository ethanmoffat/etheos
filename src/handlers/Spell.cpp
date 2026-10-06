
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

// Begin spell chant
void Spell_Request(Character* character, const client::SpellRequestClientPacket& packet)
{
	const int spell_id = packet.spell_id;

	character->timestamp = packet.timestamp;

	character->CancelSpell();

	if (character->HasSpell(spell_id))
	{
		const pub::EsfRecord* spell;

		if (!character->world->esf->TryGet(spell_id, spell))
			return;

		character->spell_id = spell_id;
		character->spell_event = new TimeEvent(character_cast_spell, character, 0.47 * spell->cast_time + character->SpellCooldownTime(), 1);
		character->world->timer.Register(character->spell_event);

		server::SpellRequestServerPacket chant;
		chant.player_id = character->PlayerID();
		chant.spell_id = spell_id;

		character->map->SendToNearby(character, chant, false);
	}
}

// Self-targeting spell cast
void Spell_Target_Self(Character* character, const client::SpellTargetSelfClientPacket& packet)
{
	Timestamp timestamp = packet.timestamp;

	if (!character->spell_event && !character->spell_ready)
		return;

	character->spell_target = Character::TargetSelf;
	character->spell_target_id = 0;

	if (character->world->config["EnforceTimestamps"])
	{
		const pub::EsfRecord& spell = character->world->esf->Get(character->spell_id);

		if (timestamp - character->timestamp < (spell.cast_time - 1) * 47 + 35
		 || timestamp - character->timestamp > std::max<int>(spell.cast_time, 1) * 50)
		{
			character->CancelSpell();
			return;
		}
	}

	character->timestamp = timestamp;

	if (character->spell_ready)
		character->SpellAct();
}

// Targeted spell cast
void Spell_Target_Other(Character* character, const client::SpellTargetOtherClientPacket& packet)
{
	Timestamp timestamp = packet.timestamp;

	if (!character->spell_event && !character->spell_ready)
		return;

	switch (packet.target_type)
	{
		case client::SpellTargetType::Player:
			character->spell_target = Character::TargetPlayer;
			character->spell_target_id = packet.victim_id;
			break;

		case client::SpellTargetType::Npc:
			character->spell_target = Character::TargetNPC;
			character->spell_target_id = packet.victim_id;
			break;

		default:
			character->CancelSpell();
			return;
	}

	if (character->world->config["EnforceTimestamps"])
	{
		const pub::EsfRecord& spell = character->world->esf->Get(character->spell_id);

		if (timestamp - character->timestamp < (spell.cast_time - 1) * 47 + 35
		 || timestamp - character->timestamp > std::max<int>(spell.cast_time, 1) * 50)
		{
			character->CancelSpell();
			return;
		}
	}

	character->timestamp = timestamp;

	if (character->spell_ready)
		character->SpellAct();
}

// Group spell cast
void Spell_Target_Group(Character* character, const client::SpellTargetGroupClientPacket& packet)
{
	Timestamp timestamp = packet.timestamp;

	if (!character->spell_event && !character->spell_ready)
		return;

	character->spell_target = Character::TargetGroup;
	character->spell_target_id = 0;

	if (character->world->config["EnforceTimestamps"])
	{
		const pub::EsfRecord& spell = character->world->esf->Get(character->spell_id);

		if (timestamp - character->timestamp < (spell.cast_time - 1) * 47 + 35
		 || timestamp - character->timestamp > std::max<int>(spell.cast_time, 1) * 50)
		{
			character->CancelSpell();
			return;
		}
	}

	character->timestamp = timestamp;

	if (character->spell_ready)
		character->SpellAct();
}

PACKET_HANDLER_REGISTER(PACKET_SPELL)
	Register(Spell_Request, Playing);
	Register(Spell_Target_Self, Playing);
	Register(Spell_Target_Other, Playing);
	Register(Spell_Target_Group, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_SPELL)

}
