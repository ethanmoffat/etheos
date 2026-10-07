
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "party.hpp"

#include "character.hpp"
#include "config.hpp"
#include "map.hpp"
#include "world.hpp"

#include "util.hpp"

#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace server = eolib::protocol::net::server;

static int HpPercentage(const Character* character)
{
	return util::clamp<int>(static_cast<int>(double(character->hp) / double(character->maxhp) * 100.0), 0, 100);
}

static server::PartyMember PartyMemberFrom(const Character* member, bool leader)
{
	server::PartyMember party_member;
	party_member.player_id = member->PlayerID();
	party_member.leader = leader;
	party_member.level = member->level;
	party_member.hp_percentage = HpPercentage(member);
	party_member.name = member->SourceName();
	return party_member;
}

Party::Party(World *world, Character *leader, Character *other)
{
	this->world = world;
	this->world->parties.push_back(this);

	this->members.push_back(leader);
	this->members.push_back(other);

	this->leader = leader;

	leader->party = this;
	other->party = this;

	this->temp_expsum = 0;

	this->RefreshMembers(leader, true);
	this->RefreshMembers(other, true);
}

void Party::Msg(Character *from, std::string message, bool echo)
{
	message = util::text_cap(message, static_cast<int>(this->world->config["ChatMaxWidth"]) - util::text_width(util::ucfirst(from->SourceName()) + "  "));

	server::TalkOpenServerPacket packet;
	packet.player_id = from->PlayerID();
	packet.message = message;

	UTIL_FOREACH(this->members, member)
	{
		member->AddChatLog("'", from->SourceName(), message);

		if (!echo && member == from)
			continue;

		member->Send(packet);
	}
}

void Party::Join(Character *character)
{
	character->party = this;

	this->members.push_back(character);

	server::PartyAddServerPacket add;
	add.member = PartyMemberFrom(character, character == this->leader);

	UTIL_FOREACH(this->members, checkcharacter)
	{
		if (checkcharacter != character)
		{
			checkcharacter->Send(add);
		}
	}

	this->RefreshMembers(character, true);
}

void Party::Leave(Character *character)
{
	if (this->members.size() > 2 && character != this->leader)
	{
		UTIL_IFOREACH(this->members, checkcharacter)
		{
			if (*checkcharacter == character)
			{
				this->members.erase(checkcharacter);
				break;
			}
		}

		character->party = 0;

		server::PartyRemoveServerPacket remove;
		remove.player_id = character->PlayerID();
		UTIL_FOREACH(this->members, checkcharacter)
		{
			if (character != checkcharacter)
			{
				checkcharacter->Send(remove);
			}
		}

		character->Send(server::PartyCloseServerPacket());
	}
	else
	{
		delete this;
	}
}

void Party::RefreshMembers(Character *character, bool create)
{
	std::vector<server::PartyMember> members;
	members.reserve(this->members.size());

	UTIL_FOREACH(this->members, member)
	{
		members.push_back(PartyMemberFrom(member, member == this->leader));
	}

	if (create)
	{
		server::PartyCreateServerPacket packet;
		packet.members = std::move(members);
		character->Send(packet);
	}
	else
	{
		server::PartyListServerPacket packet;
		packet.members = std::move(members);
		character->Send(packet);
	}
}

void Party::UpdateHP(Character *character)
{
	server::PartyAgreeServerPacket packet;
	packet.player_id = character->PlayerID();
	packet.hp_percentage = HpPercentage(character);

	UTIL_FOREACH(this->members, member)
	{
		member->Send(packet);
	}
}

void Party::ShareEXP(int exp, int sharemode, Map *map)
{
	int reward = 0;
	double sumlevel = 0;
	double members = 0;

	UTIL_FOREACH(this->members, member)
	{
		if (!member->CanInteractCombat())
			continue;

		if (member->map == map && !member->nowhere)
		{
			if (member->level == 0)
			{
				++sumlevel;
			}
			else
			{
				sumlevel += member->level;
			}
			++members;
		}
	}

	if (members == 0 || sumlevel == 0)
		return;

	server::PartyTargetGroupServerPacket packet;
	std::vector<Character*> recipients;
	bool any_level_up = false;

	UTIL_FOREACH(this->members, member)
	{
		if (!member->CanInteractCombat())
			continue;

		if (member->map != map || member->nowhere)
		{
			continue;
		}
		switch (sharemode)
		{
			case 1:
				reward = int(std::ceil(double(exp) / members));
				break;

			case 2:
				reward = int(std::ceil(double(exp) * double((member->level == 0) ? 1 : member->level) / sumlevel));
				break;
		}

		member->exp = std::min(member->exp + reward, static_cast<int>(this->world->config["MaxExp"]));

		const int old_level = member->level;
		while (member->level < static_cast<int>(this->world->config["MaxLevel"]) && member->exp >= this->world->exp_table[member->level + 1])
		{
			++member->level;
			member->statpoints += static_cast<int>(this->world->config["StatPerLevel"]);
			member->skillpoints += static_cast<int>(this->world->config["SkillPerLevel"]);
			member->CalculateStats();
		}

		const bool level_up = member->level != old_level;

		if (level_up)
		{
			server::RecoverTargetGroupServerPacket stats;
			stats.stat_points = member->statpoints;
			stats.skill_points = member->skillpoints;
			stats.max_hp = member->maxhp;
			stats.max_tp = member->maxtp;
			stats.max_sp = member->maxsp;
			member->Send(stats);
			any_level_up = true;
		}

		server::PartyExpShare gain;
		gain.player_id = member->PlayerID();
		gain.experience = reward;
		gain.level_up = level_up ? member->level : 0;
		packet.gains.push_back(gain);
		recipients.push_back(member);
	}

	// Level ups go to the whole map so nearby players see the level up emote
	if (any_level_up)
	{
		map->SendToMap(packet);
		return;
	}

	UTIL_FOREACH(recipients, recipient)
	{
		recipient->Send(packet);
	}
}

Party::~Party()
{
	if (this->world)
	{
		this->world->parties.erase(
			std::remove(this->world->parties.begin(), this->world->parties.end(), this),
			this->world->parties.end()
		);
	}

	const server::PartyCloseServerPacket close;

	UTIL_FOREACH(this->members, member)
	{
		member->party = 0;
		member->Send(close);
	}
}
