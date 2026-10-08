
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "npc.hpp"

#include "character.hpp"
#include "config.hpp"
#include "converters.hpp"
#include "eodata.hpp"
#include "map.hpp"
#include "npc_data.hpp"
#include "party.hpp"
#include "quest.hpp"
#include "timer.hpp"
#include "world.hpp"

#include "console.hpp"
#include "util.hpp"
#include "util/rpn.hpp"

#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <list>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pub = eolib::protocol::pub;
namespace server = eolib::protocol::net::server;

static double speed_table[8] = {0.9, 0.6, 1.3, 1.9, 3.7, 7.5, 15.0, 0.0};

void NPC::SetSpeedTable(std::array<double, 7> speeds)
{
	for (std::size_t i = 0; i < 7; ++i)
	{
		if (speeds[i] != 0.0)
			speed_table[i] = speeds[i];
	}
}

NPC::NPC(Map *map, short id, const eolib::protocol::Coords& loc, unsigned char spawn_type, short respawn_time, Direction direction, unsigned char index, bool temporary)
{
	this->id = id;
	this->map = map;
	this->temporary = temporary;
	this->index = index;
	this->loc = loc;
	this->spawn_loc = loc;
	this->alive = false;
	this->attack = false;
	this->totaldamage = 0;
	this->spawn_type = (std::min)(spawn_type, StationarySpawnType);
	this->respawn_time = respawn_time;
	this->direction = direction;
	this->walk_idle_for = 0;
	this->parent = 0;
}

const NPC_Data& NPC::Data() const
{
	return *this->map->world->GetNpcData(this->id);
}

const pub::EnfRecord& NPC::PubData() const
{
	return this->Data().PubData();
}

void NPC::Spawn(NPC *parent)
{
	if (this->alive)
		return;

	if (this->PubData().boss && !parent)
	{
		UTIL_FOREACH(this->map->npcs, npc)
		{
			if (npc->PubData().child)
			{
				npc->Spawn(this);
			}
		}
	}

	if (parent)
	{
		this->parent = parent;
	}

	if (this->spawn_type < StationarySpawnType)
	{
		bool found = false;
		for (int i = 0; i < 200; ++i)
		{
			if (this->temporary && i == 0)
			{
				this->loc = this->spawn_loc;
			}
			else
			{
				this->loc.x = util::rand(this->spawn_loc.x-2, this->spawn_loc.x+2);
				this->loc.y = util::rand(this->spawn_loc.y-2, this->spawn_loc.y+2);
			}

			if (this->map->Walkable(this->loc.x, this->loc.y, true) && (i > 100 || !this->map->Occupied(this->loc.x, this->loc.y, Map::NPCOnly)))
			{
				this->direction = static_cast<Direction>(util::rand(0,3));
				found = true;
				break;
			}
		}

		if (!found)
		{
			Console::Wrn("An NPC on map %i at %i,%i is being placed by linear scan of spawn area (%s)", this->map->id, this->spawn_loc.x, this->spawn_loc.y, this->map->world->enf->Get(this->id).name.c_str());
			for (this->loc.x = this->spawn_loc.x-2; this->loc.x <= this->spawn_loc.x+2; ++this->loc.x)
			{
				for (this->loc.y = this->spawn_loc.y-2; this->loc.y <= this->spawn_loc.y+2; ++this->loc.y)
				{
					if (this->map->Walkable(this->loc.x, this->loc.y, true))
					{
						Console::Wrn("Placed at valid location: %i,%i", this->loc.x, this->loc.y);
						found = true;
						goto end_linear_scan;
					}
				}
			}
		}
		end_linear_scan:

		if (!found)
		{
			Console::Err("NPC couldn't spawn anywhere valid!");
		}
	}

	this->alive = true;
	this->hp = this->PubData().hp;
	this->last_act = Timer::GetTime();
	this->last_talk = Timer::GetTime();
	this->act_speed = speed_table[this->spawn_type];

	server::RangeReplyServerPacket range_reply;
	range_reply.nearby.npcs.push_back(Converters::NpcMapInfoFrom(this));

	this->map->SendToNearby(this, range_reply);
}

void NPC::Act()
{
	// Needed for the server startup spawn to work properly
	if (this->PubData().child && !this->parent)
	{
		UTIL_FOREACH(this->map->npcs, npc)
		{
			if (npc->PubData().boss)
			{
				this->parent = npc;
				break;
			}
		}
	}

	this->last_act += double(util::rand(int(this->act_speed * 750.0), int(this->act_speed * 1250.0))) / 1000.0;

	if (this->spawn_type == StationarySpawnType)
	{
		return;
	}

	Character *attacker = 0;
	unsigned char attacker_distance = static_cast<int>(this->map->world->config["NPCChaseDistance"]);
	unsigned short attacker_damage = 0;

	if (this->PubData().type == pub::NpcType::Passive || this->PubData().type == pub::NpcType::Aggressive)
	{
		UTIL_FOREACH_CREF(this->damagelist, opponent)
		{
			if (opponent->attacker->map != this->map || opponent->attacker->nowhere || opponent->last_hit < Timer::GetTime() - static_cast<double>(this->map->world->config["NPCBoredTimer"]))
			{
				continue;
			}

			int distance = util::path_length(opponent->attacker->loc.x, opponent->attacker->loc.y, this->loc.x, this->loc.y);

			if (distance == 0)
				distance = 1;

			if ((distance < attacker_distance) || (distance == attacker_distance && opponent->damage > attacker_damage))
			{
				attacker = opponent->attacker;
				attacker_damage = opponent->damage;
				attacker_distance = distance;
			}
		}

		if (this->parent)
		{
			UTIL_FOREACH_CREF(this->parent->damagelist, opponent)
			{
				if (opponent->attacker->map != this->map || opponent->attacker->nowhere || opponent->last_hit < Timer::GetTime() - static_cast<double>(this->map->world->config["NPCBoredTimer"]))
				{
					continue;
				}

				int distance = util::path_length(opponent->attacker->loc.x, opponent->attacker->loc.y, this->loc.x, this->loc.y);

				if (distance == 0)
					distance = 1;

				if ((distance < attacker_distance) || (distance == attacker_distance && opponent->damage > attacker_damage))
				{
					attacker = opponent->attacker;
					attacker_damage = opponent->damage;
					attacker_distance = distance;
				}
			}
		}
	}

	if (this->PubData().type == pub::NpcType::Aggressive || (this->parent && attacker))
	{
		Character *closest = 0;
		unsigned char closest_distance = static_cast<int>(this->map->world->config["NPCChaseDistance"]);

		if (attacker)
		{
			closest = attacker;
			closest_distance = std::min(closest_distance, attacker_distance);
		}

		UTIL_FOREACH(this->map->characters, character)
		{
			if (character->IsHideNpc() || !character->CanInteractCombat())
				continue;

			int distance = util::path_length(character->loc.x, character->loc.y, this->loc.x, this->loc.y);

			if (distance == 0)
				distance = 1;

			if (distance < closest_distance)
			{
				closest = character;
				closest_distance = distance;
			}
		}

		if (closest)
		{
			attacker = closest;
		}
	}

	if (attacker)
	{
		int xdiff = this->loc.x - attacker->loc.x;
		int ydiff = this->loc.y - attacker->loc.y;
		int absxdiff = std::abs(xdiff);
		int absydiff = std::abs(ydiff);

		if ((absxdiff == 1 && absydiff == 0) || (absxdiff == 0 && absydiff == 1) || (absxdiff == 0 && absydiff == 0))
		{
			this->Attack(attacker);
			return;
		}
		else if (absxdiff > absydiff)
		{
			if (xdiff < 0)
			{
				this->direction = Direction::Right;
			}
			else
			{
				this->direction = Direction::Left;
			}
		}
		else
		{
			if (ydiff < 0)
			{
				this->direction = Direction::Down;
			}
			else
			{
				this->direction = Direction::Up;
			}
		}

		if (this->Walk(this->direction) == Map::WalkFail)
		{
			if (this->direction == Direction::Up || this->direction == Direction::Down)
			{
				if (xdiff < 0)
				{
					this->direction = Direction::Right;
				}
				else
				{
					this->direction = Direction::Left;
				}
			}

			if (this->Walk(this->direction) == Map::WalkFail)
			{
				this->Walk(static_cast<Direction>(util::rand(0,3)));
			}
		}
	}
	else
	{
		// Random walking

		int act;
		if (this->walk_idle_for == 0)
		{
			act = util::rand(1,10);
		}
		else
		{
			--this->walk_idle_for;
			act = 11;
		}

		if (act >= 1 && act <= 6) // 60% chance walk foward
		{
			this->Walk(this->direction);
		}

		if (act >= 7 && act <= 9) // 30% change direction
		{
			this->Walk(static_cast<Direction>(util::rand(0,3)));
		}

		if (act == 10) // 10% take a break
		{
			this->walk_idle_for = util::rand(1,4);
		}
	}
}

void NPC::Talk()
{
	const auto& data = this->Data();

	if (data.talk_phrases.empty())
	{
		return;
	}

	this->last_talk = Timer::GetTime();

	std::vector<NPC*> talk_candidates;
	talk_candidates.reserve(this->map->npcs.size());

	UTIL_FOREACH(this->map->npcs, npc)
	{
		if (npc->id == this->id && npc->spawn_loc.x == this->spawn_loc.x && npc->spawn_loc.y == this->spawn_loc.y)
		{
			npc->last_talk = this->last_talk;
			if (npc->alive && npc->InCharacterRange())
			{
				talk_candidates.push_back(npc);
			}
		}
	}

	if (talk_candidates.empty())
	{
		return;
	}

	if (util::rand(0.0, 1.0) <= data.talk_chance)
	{
		NPC* talker = talk_candidates[util::rand(0, talk_candidates.size() - 1)];
		std::string phrase = data.talk_phrases[util::rand(0, data.talk_phrases.size() - 1)];
		talker->Say(phrase);
	}
}

bool NPC::InCharacterRange()
{
	UTIL_FOREACH(this->map->characters, character)
	{
		if (character->InRange(this))
		{
			return true;
		}
	}
	return false;
}

bool NPC::Walk(Direction direction)
{
	return this->map->Walk(this, direction);
}

void NPC::Damage(Character *from, int amount, int spell_id)
{
	int limitamount = std::min(this->hp, amount);

	if (this->map->world->config["LimitDamage"])
	{
		amount = limitamount;
	}

	if (this->PubData().type == pub::NpcType::Passive || this->PubData().type == pub::NpcType::Aggressive)
	{
		this->hp -= amount;
	}
	else
	{
		this->hp = 0;
		amount = 0;
	}

	if (this->totaldamage + limitamount > this->totaldamage)
		this->totaldamage += limitamount;

	std::unique_ptr<NPC_Opponent> opponent(new NPC_Opponent);
	bool found = false;

	UTIL_FOREACH_CREF(this->damagelist, checkopp)
	{
		if (checkopp->attacker == from)
		{
			found = true;

			if (checkopp->damage + limitamount > checkopp->damage)
				checkopp->damage += limitamount;

			checkopp->last_hit = Timer::GetTime();
		}
	}

	if (!found)
	{
		opponent->attacker = from;
		opponent->damage = limitamount;
		opponent->last_hit = Timer::GetTime();
		opponent->attacker->unregister_npc.push_back(this);
		this->damagelist.emplace_back(std::move(opponent));
	}

	if (this->hp > 0)
	{
		const int hp_percentage = util::clamp<int>(static_cast<int>(double(this->hp) / double(this->PubData().hp) * 100.0), 0, 100);

		if (spell_id == -1)
		{
			server::NpcReplyServerPacket reply;
			reply.player_id = from->PlayerID();
			reply.player_direction = from->direction;
			reply.npc_index = this->index;
			reply.damage = amount;
			reply.hp_percentage = hp_percentage;

			server::NpcReplyServerPacket attacker_reply = reply;
			attacker_reply.kill_steal_protection = server::NpcKillStealProtectionState::Unprotected;

			from->Send(attacker_reply);
			this->map->SendToNearby(this, reply, from);
		}
		else
		{
			server::CastReplyServerPacket reply;
			reply.spell_id = spell_id;
			reply.caster_id = from->PlayerID();
			reply.caster_direction = from->direction;
			reply.npc_index = this->index;
			reply.damage = amount;
			reply.hp_percentage = hp_percentage;

			server::CastReplyServerPacket attacker_reply = reply;
			attacker_reply.caster_tp = from->tp;
			attacker_reply.kill_steal_protection = server::NpcKillStealProtectionState::Unprotected;

			from->Send(attacker_reply);
			this->map->SendToNearby(this, reply, from);
		}
	}
	else
	{
		this->Killed(from, amount, spell_id);
		// *this may not be valid here
		return;
	}
}

void NPC::RemoveFromView(Character *target)
{
	// Walks the NPC to a far corner of the map before removing it
	server::NpcUpdatePosition position;
	position.npc_index = this->index;
	const int corner = (target->loc.x > 200 && target->loc.y > 200) ? 0 : 252;
	position.coords.x = corner;
	position.coords.y = corner;
	position.direction = Direction::Down;

	server::NpcPlayerServerPacket walk;
	walk.positions.push_back(position);

	server::NpcSpecServerPacket remove;
	remove.npc_killed_data.npc_index = this->index;

	target->Send(walk);
	target->Send(remove);
}

void NPC::Killed(Character *from, int amount, int spell_id)
{
	double droprate = this->map->world->config["DropRate"];
	double exprate = this->map->world->config["ExpRate"];
	int sharemode = this->map->world->config["ShareMode"];
	int partysharemode = this->map->world->config["PartyShareMode"];
	int dropratemode = this->map->world->config["DropRateMode"];
	std::set<Party *> parties;

	int most_damage_counter = 0;
	Character *most_damage = nullptr;
	NPC_Drop *drop = nullptr;

	this->alive = false;

	this->dead_since = int(Timer::GetTime());

	if (dropratemode == 1)
	{
		std::vector<NPC_Drop *> drops;

		UTIL_FOREACH_CREF(this->Data().drops, checkdrop)
		{
			if (util::rand(0.0, 100.0) <= checkdrop->chance * droprate)
			{
				drops.push_back(checkdrop.get());
			}
		}

		if (drops.size() > 0)
		{
			drop = drops[util::rand(0, drops.size()-1)];
		}
	}
	else if (dropratemode == 2)
	{
		UTIL_FOREACH_CREF(this->Data().drops, checkdrop)
		{
			if (util::rand(0.0, 100.0) <= checkdrop->chance * droprate)
			{
				drop = checkdrop.get();
				break;
			}
		}
	}
	else if (dropratemode == 3)
	{
		double roll = util::rand(0.0, this->Data().drops_chance_total);

		UTIL_FOREACH_CREF(this->Data().drops, checkdrop)
		{
			if (roll >= checkdrop->chance_offset && roll < checkdrop->chance_offset+checkdrop->chance)
			{
				drop = checkdrop.get();
				break;
			}
		}
	}

	if (sharemode == 1)
	{
		UTIL_FOREACH_CREF(this->damagelist, opponent)
		{
			if (opponent->damage > most_damage_counter)
			{
				most_damage_counter = opponent->damage;
				most_damage = opponent->attacker;
			}
		}
	}

	int dropuid = 0;
	int dropid = 0;
	int dropamount = 0;
	Character* drop_winner = nullptr;

	if (drop)
	{
		dropid = drop->id;
		dropamount = std::min<int>(util::rand(drop->min, drop->max), this->map->world->config["MaxItem"]);

		if (!this->map->world->eif->Exists(dropid) || dropamount <= 0)
			goto abort_drop;

		dropuid = this->map->GenerateItemID();

		std::shared_ptr<Map_Item> newitem(std::make_shared<Map_Item>(dropuid, dropid, dropamount, this->loc.x, this->loc.y, from->PlayerID(), Timer::GetTime() + static_cast<int>(this->map->world->config["ProtectNPCDrop"])));
		this->map->items.push_back(newitem);

		// Selects a random number between 0 and maxhp, and decides the winner based on that
		switch (sharemode)
		{
			case 0:
				drop_winner = from;
				break;

			case 1:
				drop_winner = most_damage;
				break;

			case 2:
			{
				int rewarded_hp = util::rand(0, this->totaldamage - 1);
				int count_hp = 0;
				UTIL_FOREACH_CREF(this->damagelist, opponent)
				{
					if (opponent->attacker->InRange(this))
					{
						if (rewarded_hp >= count_hp && rewarded_hp < opponent->damage)
						{
							drop_winner = opponent->attacker;
							break;
						}

						count_hp += opponent->damage;
					}
				}
			}
				break;

			case 3:
			{
				int rand = util::rand(0, this->damagelist.size() - 1);
				int i = 0;
				UTIL_FOREACH_CREF(this->damagelist, opponent)
				{
					if (opponent->attacker->InRange(this))
					{
						if (rand == i++)
						{
							drop_winner = opponent->attacker;
							break;
						}
					}
				}
			}
				break;
		}
	}
	abort_drop:

	if (drop_winner)
		this->map->items.back()->owner = drop_winner->PlayerID();

	const Character* killer = drop_winner ? drop_winner : from;

	server::NpcKilledData killed_data;
	killed_data.killer_id = killer->PlayerID();
	killed_data.killer_direction = killer->direction;
	killed_data.npc_index = this->index;
	killed_data.drop_index = dropuid;
	killed_data.drop_id = dropid;
	killed_data.drop_coords = this->loc;
	killed_data.drop_amount = dropamount;
	killed_data.damage = amount;

	// tuple fields: Character, did they attack the NPC, did they level up
	std::vector<std::tuple<Character*, bool, bool>> recipients;

	UTIL_FOREACH(this->map->characters, character)
	{
		const auto findopp = std::find_if(
			UTIL_RANGE(this->damagelist),
			[character](const std::unique_ptr<NPC_Opponent>& opponent) { return opponent->attacker == character; }
		);

		if (findopp != this->damagelist.end() || character->InRange(this))
		{
			bool level_up = false;

			if (this->PubData().experience != 0)
			{
				if (findopp != this->damagelist.end())
				{
					int reward;
					switch (sharemode)
					{
						case 0:
							if (character == from)
							{
								reward = int(std::ceil(double(this->PubData().experience) * exprate));

								if (reward > 0)
								{
									if (partysharemode)
									{
										if (character->party)
										{
											character->party->ShareEXP(reward, partysharemode, this->map);
										}
										else
										{
											character->exp += reward;
										}
									}
									else
									{
										character->exp += reward;
									}
								}
							}
							break;

						case 1:
							if (character == most_damage)
							{
								reward = int(std::ceil(double(this->PubData().experience) * exprate));

								if (reward > 0)
								{
									if (partysharemode)
									{
										if (character->party)
										{
											character->party->ShareEXP(reward, partysharemode, this->map);
										}
										else
										{
											character->exp += reward;
										}
									}
									else
									{
										character->exp += reward;
									}
								}
							}
							break;

						case 2:
							reward = int(std::ceil(double(this->PubData().experience) * exprate * (double((*findopp)->damage) / double(this->totaldamage))));

							if (reward > 0)
							{
								if (partysharemode)
								{
									if (character->party)
									{
										character->party->temp_expsum += reward;
										parties.insert(character->party);
									}
									else
									{
										character->exp += reward;
									}
								}
								else
								{
									character->exp += reward;
								}
							}
							break;

						case 3:
							reward = int(std::ceil(double(this->PubData().experience) * exprate * (double(this->damagelist.size()) / 1.0)));

							if (reward > 0)
							{
								if (partysharemode)
								{
									if (character->party)
									{
										character->party->temp_expsum += reward;
									}
									else
									{
										character->exp += reward;
									}
								}
								else
								{
									character->exp += reward;
								}
							}
							break;
					}

					character->exp = std::min(character->exp, static_cast<int>(this->map->world->config["MaxExp"]));

					while (character->level < static_cast<int>(this->map->world->config["MaxLevel"]) && character->exp >= this->map->world->exp_table[character->level+1])
					{
						level_up = true;
						++character->level;
						character->statpoints += static_cast<int>(this->map->world->config["StatPerLevel"]);
						character->skillpoints += static_cast<int>(this->map->world->config["SkillPerLevel"]);
						character->CalculateStats();
					}

				}
			}

			recipients.emplace_back(character, findopp != this->damagelist.end(), level_up);
		}
	}

	const auto killer_recipient = std::find_if(
		UTIL_RANGE(recipients),
		[&](const auto& recipient) { return std::get<0>(recipient) == killer; }
	);
	const bool killer_level_up = killer_recipient != recipients.end() && std::get<2>(*killer_recipient);

	for (const auto& [character, attacker, level_up] : recipients)
	{
		// The kill packet can only announce a level up for killer_id
		if (level_up && character != killer)
		{
			server::ItemAcceptServerPacket packet;
			packet.player_id = character->PlayerID();
			this->map->SendToNearby(character, packet, false);
		}

		if (character == from)
		{
			const bool accept = killer_level_up || level_up;

			if (spell_id == -1 && accept)
			{
				server::NpcAcceptServerPacket packet;
				packet.npc_killed_data = killed_data;
				packet.experience = character->exp;
				if (level_up)
					packet.level_up = Converters::LevelUpStatsFrom(character);
				character->Send(packet);
			}
			else if (spell_id == -1)
			{
				server::NpcSpecServerPacket packet;
				packet.npc_killed_data = killed_data;
				packet.experience = character->exp;
				character->Send(packet);
			}
			else if (accept)
			{
				server::CastAcceptServerPacket packet;
				packet.spell_id = spell_id;
				packet.npc_killed_data = killed_data;
				packet.caster_tp = character->tp;
				packet.experience = character->exp;
				if (level_up)
					packet.level_up = Converters::LevelUpStatsFrom(character);
				character->Send(packet);
			}
			else
			{
				server::CastSpecServerPacket packet;
				packet.spell_id = spell_id;
				packet.npc_killed_data = killed_data;
				packet.caster_tp = character->tp;
				packet.experience = character->exp;
				character->Send(packet);
			}

			continue;
		}

		if (spell_id == -1 && killer_level_up)
		{
			server::NpcAcceptServerPacket packet;
			packet.npc_killed_data = killed_data;
			character->Send(packet);
		}
		else if (spell_id == -1)
		{
			server::NpcSpecServerPacket packet;
			packet.npc_killed_data = killed_data;
			character->Send(packet);
		}
		else if (killer_level_up)
		{
			server::CastAcceptServerPacket packet;
			packet.spell_id = spell_id;
			packet.npc_killed_data = killed_data;
			character->Send(packet);
		}
		else
		{
			server::CastSpecServerPacket packet;
			packet.spell_id = spell_id;
			packet.npc_killed_data = killed_data;
			character->Send(packet);
		}

		// Experience shared with other attackers goes in separate packets, as only the killer gets it in the kill packet
		if (sharemode != 0 && attacker)
		{
			if (level_up)
			{
				server::RecoverTargetGroupServerPacket stats;
				stats.stat_points = character->statpoints;
				stats.skill_points = character->skillpoints;
				stats.max_hp = character->maxhp;
				stats.max_tp = character->maxtp;
				stats.max_sp = character->maxsp;
				character->Send(stats);
			}

			server::RecoverReplyServerPacket recover_packet;
			recover_packet.experience = character->exp;
			recover_packet.karma = character->karma;
			recover_packet.level_up = level_up ? character->level : 0;

			if (level_up)
			{
				recover_packet.stat_points = character->statpoints;
				recover_packet.skill_points = character->skillpoints;
			}

			character->Send(recover_packet);
		}
	}

	UTIL_FOREACH(parties, party)
	{
		party->ShareEXP(party->temp_expsum, partysharemode, this->map);
		party->temp_expsum = 0;
	}

	UTIL_FOREACH_CREF(this->damagelist, opponent)
	{
		opponent->attacker->unregister_npc.erase(
			std::remove(UTIL_RANGE(opponent->attacker->unregister_npc), this),
			opponent->attacker->unregister_npc.end()
		);
	}

	this->damagelist.clear();
	this->totaldamage = 0;

	short childid = -1;

	if (this->PubData().boss)
	{
		std::vector<NPC*> child_npcs;

		UTIL_FOREACH(this->map->npcs, npc)
		{
			if (npc->PubData().child && !npc->PubData().boss && npc->alive)
			{
				child_npcs.push_back(npc);
			}
		}

		UTIL_FOREACH(child_npcs, npc)
		{
			if (!npc->temporary && (childid == -1 || childid == npc->id))
			{
				npc->Die(false);
				childid = npc->id;
			}
			else
			{
				npc->Die(true);
			}
		}
	}

	if (childid != -1)
	{
		server::NpcJunkServerPacket packet;
		packet.npc_id = childid;
		this->map->SendToMap(packet);
	}

	if (this->temporary)
	{
		this->map->npcs.erase(
			std::remove(this->map->npcs.begin(), this->map->npcs.end(), this),
			this->map->npcs.end()
		);
	}

	UTIL_FOREACH(from->quests, q)
	{
		if (!q.second || q.second->GetQuest()->Disabled())
			continue;

		q.second->KilledNPC(this->id);
	}

	if (this->temporary)
	{
		delete this;
		return;
	}
}

void NPC::Die(bool show)
{
	if (!this->alive)
		return;

	this->alive = false;
	this->parent = 0;
	this->dead_since = int(Timer::GetTime());

	UTIL_FOREACH_CREF(this->damagelist, opponent)
	{
		opponent->attacker->unregister_npc.erase(
			std::remove(UTIL_RANGE(opponent->attacker->unregister_npc), this),
			opponent->attacker->unregister_npc.end()
		);
	}

	this->damagelist.clear();
	this->totaldamage = 0;

	if (show)
	{
		server::NpcSpecServerPacket packet;
		packet.npc_killed_data.npc_index = this->index;
		packet.npc_killed_data.drop_coords = this->loc;
		packet.npc_killed_data.damage = this->hp;

		this->map->SendToNearby(this, packet);
	}

	if (this->temporary)
	{
		this->map->npcs.erase(
			std::remove(this->map->npcs.begin(), this->map->npcs.end(), this),
			this->map->npcs.end()
		);

		delete this;
	}
}

void NPC::Attack(Character *target)
{
	int amount = util::rand(this->PubData().min_damage, this->PubData().max_damage + static_cast<int>(this->map->world->config["NPCAdjustMaxDam"]));
	double rand = util::rand(0.0, 1.0);
	// Checks if target is facing you
	bool critical = std::abs(int(target->direction) - int(this->direction)) != 2 || rand < static_cast<double>(this->map->world->config["CriticalRate"]);

	std::unordered_map<std::string, double> formula_vars;

	this->FormulaVars(formula_vars);
	target->FormulaVars(formula_vars, "target_");
	formula_vars["modifier"] = 1.0 / static_cast<double>(this->map->world->config["MobRate"]);
	formula_vars["damage"] = amount;
	formula_vars["critical"] = critical;

	amount = static_cast<int>(rpn_eval(rpn_parse(this->map->world->formulas_config["damage"]), formula_vars));
	double hit_rate = rpn_eval(rpn_parse(this->map->world->formulas_config["hit_rate"]), formula_vars);

	if (rand > hit_rate)
	{
		amount = 0;
	}

	amount = std::max(amount, 0);

	int limitamount = std::min(amount, int(target->hp));

	if (this->map->world->config["LimitDamage"])
	{
		amount = limitamount;
	}

	target->hp -= limitamount;
	if (target->party)
	{
		target->party->UpdateHP(target);
	}

	int xdiff = this->loc.x - target->loc.x;
	int ydiff = this->loc.y - target->loc.y;

	if (std::abs(xdiff) > std::abs(ydiff))
	{
		if (xdiff < 0)
		{
			this->direction = Direction::Right;
		}
		else
		{
			this->direction = Direction::Left;
		}
	}
	else
	{
		if (ydiff < 0)
		{
			this->direction = Direction::Down;
		}
		else
		{
			this->direction = Direction::Up;
		}
	}

	server::NpcUpdateAttack attack;
	attack.npc_index = this->index;
	attack.killed = (target->hp == 0) ? server::PlayerKilledState::Killed : server::PlayerKilledState::Alive;
	attack.direction = this->direction;
	attack.player_id = target->PlayerID();
	attack.damage = amount;
	attack.hp_percentage = util::clamp<int>(static_cast<int>(double(target->hp) / double(target->maxhp) * 100.0), 0, 100);

	server::NpcPlayerServerPacket packet;
	packet.attacks.push_back(attack);

	this->map->SendToNearby(target, packet, false);

	if (target->hp == 0)
	{
		target->DeathRespawn();
	}

	packet.hp = target->hp;
	packet.tp = target->tp;

	target->Send(packet);
}

void NPC::Say(const std::string& message)
{
	this->map->Msg(this, message);
}

#define v(x) vars[prefix + #x] = x;
#define vv(x, n) vars[prefix + n] = x;
#define vd(x) vars[prefix + #x] = data.x;

void NPC::FormulaVars(std::unordered_map<std::string, double> &vars, std::string prefix)
{
	const pub::EnfRecord& data = this->PubData();
	vv(1, "npc") v(hp) vv(data.hp, "maxhp")
	vv(data.min_damage, "mindam") vv(data.max_damage, "maxdam")
	vd(accuracy) vd(evade) vd(armor)
	vv(loc.x, "x") vv(loc.y, "y") vv(static_cast<int>(direction), "direction") vv(map->id, "mapid")
}

#undef vd
#undef vv
#undef v

NPC::~NPC()
{
	UTIL_FOREACH(this->map->characters, character)
	{
		if (character->npc == this)
		{
			character->npc = 0;
			character->npc_type = pub::NpcType::Friendly;
		}
	}

	UTIL_FOREACH_CREF(this->damagelist, opponent)
	{
		opponent->attacker->unregister_npc.erase(
			std::remove(UTIL_RANGE(opponent->attacker->unregister_npc), this),
			opponent->attacker->unregister_npc.end()
		);
	}
}
