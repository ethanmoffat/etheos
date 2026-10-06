/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../converters.hpp"
#include "../eodata.hpp"
#include "../map.hpp"
#include "../party.hpp"
#include "../quest.hpp"
#include "../timer.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/data/eo_numeric_limits.hpp>
#include <eolib/data/number_encoder.hpp>
#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Handlers
{

// ByteCoords x and y are this value when an item is dropped at the player's feet
constexpr int DropAtFeetCoord = 255;

static int DecodeByteCoord(int value)
{
	const std::uint8_t byte = static_cast<std::uint8_t>(value);
	return eolib::data::NumberEncoder::DecodeNumber(&byte, 1);
}

// Player using an item
void Item_Use(Character* character, const client::ItemUseClientPacket& packet)
{
	if (character->trading) return;

	const int id = packet.item_id;

	if (!character->HasItem(id))
	{
		server::ItemAgreeServerPacket reply;
		reply.item_id = id;
		character->Send(reply);
		return;
	}

	const pub::EifRecord& item = character->world->eif->Get(id);
	server::ItemReplyServerPacket reply;
	reply.item_type = item.type;
	reply.used_item.id = id;

	auto UseItem = [&reply](Character* character, int id)
	{
		character->DelItem(id, 1);

		reply.used_item.amount = character->HasItem(id);
		reply.weight = Converters::WeightFrom(character);

		character->Send(reply);

		UTIL_FOREACH(character->quests, q)
		{
			if (!q.second || q.second->GetQuest()->Disabled())
				continue;

			q.second->UsedItem(id);
		}
	};

	switch (item.type)
	{
		case pub::ItemType::Teleport:
		{
			if (!character->map->scroll)
				break;

			const ItemScrollSpec scroll = AsScroll(item);

			if (scroll.map == 0)
			{
				if (character->mapid == character->SpawnMap() && character->loc == character->SpawnLoc())
					break;
			}
			else
			{
				if (character->mapid == scroll.map && character->loc.x == scroll.x && character->loc.y == scroll.y)
					break;
			}

			if (scroll.map == 0)
			{
				character->Warp(character->SpawnMap(), character->SpawnLoc().x, character->SpawnLoc().y, WarpEffect::Scroll);
			}
			else
			{
				character->Warp(scroll.map, scroll.x, scroll.y, WarpEffect::Scroll);
			}

			UseItem(character, id);
		}
		break;

		case pub::ItemType::Heal:
		{
			int hpgain = item.hp;
			int tpgain = item.tp;

			if (character->world->config["LimitDamage"])
			{
				hpgain = std::min(hpgain, character->maxhp - character->hp);
				tpgain = std::min(tpgain, character->maxtp - character->tp);
			}

			hpgain = std::max(hpgain, 0);
			tpgain = std::max(tpgain, 0);

			if (hpgain == 0 && tpgain == 0)
				break;

			character->hp += hpgain;
			character->tp += tpgain;

			if (!character->world->config["LimitDamage"])
			{
				character->hp = std::min(character->hp, character->maxhp);
				character->tp = std::min(character->tp, character->maxtp);
			}

			server::ItemReplyServerPacket::ItemTypeDataHeal heal;
			heal.hp_gain = hpgain;
			heal.hp = character->hp;
			heal.tp = character->tp;
			reply.item_type_data = heal;

			server::RecoverAgreeServerPacket recover_packet;
			recover_packet.player_id = character->PlayerID();
			recover_packet.heal_hp = hpgain;
			recover_packet.hp_percentage = util::clamp<int>(static_cast<int>(double(character->hp) / double(character->maxhp) * 100.0), 0, 100);

			character->map->SendToNearby(character, recover_packet, false);

			if (character->party)
			{
				character->party->UpdateHP(character);
			}

			UseItem(character, id);
		}
		break;

		case pub::ItemType::HairDye:
		{
			const ItemHairDyeSpec dye = AsHairDye(item);

			if (character->haircolor == dye.hair_color)
				break;

			character->haircolor = dye.hair_color;

			server::ItemReplyServerPacket::ItemTypeDataHairDye hair_dye;
			hair_dye.hair_color = dye.hair_color;
			reply.item_type_data = hair_dye;

			server::AvatarChange::ChangeTypeDataHairColor hair_color;
			hair_color.hair_color = dye.hair_color;

			server::AvatarAgreeServerPacket avatar_agree;
			avatar_agree.change = server::AvatarChange::ForHairColor(hair_color);
			avatar_agree.change.player_id = character->PlayerID();

			character->map->SendToNearby(character, avatar_agree, false);

			UseItem(character, id);
		}
		break;

		case pub::ItemType::Alcohol:
		{
			character->drunk_until = Timer::GetTime() + Character::DrunkSeconds(AsAlcohol(item).potency);
			UseItem(character, id);
		}
		break;

		case pub::ItemType::EffectPotion:
		{
			const ItemEffectPotionSpec potion = AsEffectPotion(item);

			server::ItemReplyServerPacket::ItemTypeDataEffectPotion effect;
			effect.effect_id = potion.effect;
			reply.item_type_data = effect;

			character->Effect(potion.effect, false);

			UseItem(character, id);
		}
		break;

		case pub::ItemType::CureCurse:
		{
			bool found = false;

			for (std::size_t i = 0; i < character->paperdoll.size(); ++i)
			{
				if (character->world->eif->Get(character->paperdoll[i]).special == pub::ItemSpecial::Cursed)
				{
					character->paperdoll[i] = 0;
					found = true;
				}
			}

			if (!found)
				break;

			character->CalculateStats();

			server::ItemReplyServerPacket::ItemTypeDataCureCurse cure_curse;
			cure_curse.stats = Converters::CharacterStatsEquipmentChangeFrom(character);
			reply.item_type_data = cure_curse;

			server::AvatarAgreeServerPacket avatar_agree;
			avatar_agree.change = Converters::EquipmentChangeFrom(character);

			character->map->SendToNearby(character, avatar_agree, false);

			UseItem(character, id);
		}
		break;

		case pub::ItemType::ExpReward:
		{
			bool level_up = false;

			character->exp += AsExpReward(item).exp;

			character->exp = std::min(character->exp, static_cast<int>(character->map->world->config["MaxExp"]));

			while (character->level < static_cast<int>(character->map->world->config["MaxLevel"])
				&& character->exp >= character->map->world->exp_table[character->level+1])
			{
				level_up = true;
				++character->level;
				character->statpoints += static_cast<int>(character->map->world->config["StatPerLevel"]);
				character->skillpoints += static_cast<int>(character->map->world->config["SkillPerLevel"]);
				character->CalculateStats();
			}

			reply.item_type_data = Converters::ItemExpRewardFrom(character, level_up);

			if (level_up)
			{
				server::ItemAcceptServerPacket accept;
				accept.player_id = character->PlayerID();
				character->map->SendToNearby(character, accept, false);
			}

			UseItem(character, id);
		}
		break;

		default:
			return;
	}
}

// Drop an item on the ground
void Item_Drop(Character* character, const client::ItemDropClientPacket& packet)
{
	if (character->trading) return;
	if (!character->CanInteractItems()) return;

	const int id = packet.item.id;

	if (character->world->eif->Get(id).special == pub::ItemSpecial::Lore)
	{
		return;
	}

	const int amount = std::min<int>(packet.item.amount, character->world->config["MaxDrop"]);

	if (amount <= 0)
	{
		return;
	}

	eolib::protocol::Coords coords = character->loc;

	if (packet.coords.x != DropAtFeetCoord || packet.coords.y != DropAtFeetCoord)
	{
		coords.x = DecodeByteCoord(packet.coords.x);
		coords.y = DecodeByteCoord(packet.coords.y);
	}

	const int distance = util::path_length(coords.x, coords.y, character->loc.x, character->loc.y);

	if (distance > static_cast<int>(character->world->config["DropDistance"]))
	{
		return;
	}

	if (!character->map->Walkable(coords.x, coords.y))
	{
		return;
	}

	if (character->HasItem(id) < amount || character->mapid == static_cast<int>(character->world->config["JailMap"]))
	{
		return;
	}

	std::shared_ptr<Map_Item> item = character->map->AddItem(id, amount, coords.x, coords.y, character);

	if (!item)
	{
		return;
	}

	item->owner = character->PlayerID();
	item->unprotecttime = Timer::GetTime() + static_cast<double>(character->world->config["ProtectPlayerDrop"]);
	character->DelItem(id, amount);

	server::ItemDropServerPacket reply;
	reply.dropped_item.id = id;
	reply.dropped_item.amount = amount;
	reply.remaining_amount = character->HasItem(id);
	reply.item_index = item->uid;
	reply.coords = coords;
	reply.weight = Converters::WeightFrom(character);
	character->Send(reply);
}

// Destroying an item
void Item_Junk(Character* character, const client::ItemJunkClientPacket& packet)
{
	if (character->trading) return;

	const int id = packet.item.id;
	const int amount = packet.item.amount;

	// The reply reports the junked amount as a three
	if (amount <= 0 || amount >= static_cast<int>(eolib::data::EoNumericLimits::ThreeMax))
		return;

	if (character->HasItem(id) < amount)
		return;

	character->DelItem(id, amount);

	server::ItemJunkServerPacket reply;
	reply.junked_item.id = id;
	reply.junked_item.amount = amount;
	reply.remaining_amount = character->HasItem(id);
	reply.weight = Converters::WeightFrom(character);
	character->Send(reply);
}

// Retrieve an item from the ground
void Item_Get(Character* character, const client::ItemGetClientPacket& packet)
{
	std::shared_ptr<Map_Item> item = character->map->GetItem(packet.item_index);

	if (!item)
	{
		return;
	}

	const int distance = util::path_length(item->loc.x, item->loc.y, character->loc.x, character->loc.y);

	if (distance > static_cast<int>(character->world->config["DropDistance"]))
	{
		return;
	}

	if (item->owner != character->PlayerID() && item->unprotecttime > Timer::GetTime())
	{
		return;
	}

	const int taken = character->CanHoldItem(item->id, item->amount);

	if (taken <= 0)
	{
		return;
	}

	character->AddItem(item->id, taken);

	server::ItemGetServerPacket reply;
	reply.taken_item_index = item->uid;
	reply.taken_item.id = item->id;
	reply.taken_item.amount = taken;
	reply.weight = Converters::WeightFrom(character);
	character->Send(reply);

	character->map->DelSomeItem(item->uid, taken, character);
}

PACKET_HANDLER_REGISTER(PACKET_ITEM)
	Register(Item_Use, Playing);
	Register(Item_Drop, Playing);
	Register(Item_Junk, Playing);
	Register(Item_Get, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_ITEM)

}
