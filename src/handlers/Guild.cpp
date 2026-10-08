
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../eoclient.hpp"
#include "../eodata.hpp"
#include "../guild.hpp"
#include "../map.hpp"
#include "../npc.hpp"
#include "../player.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <cstddef>
#include <list>
#include <memory>
#include <string>

namespace Handlers
{

static bool IsGuildSessionValid(Character* character, int session_id)
{
	return character->npc_type == pub::NpcType::Guild && character->npc_session_id == session_id;
}

// Requested to create a guild
void Guild_Request(Character* character, const client::GuildRequestClientPacket& packet)
{
	std::string tag = packet.guild_tag;
	std::string name = packet.guild_name;

	if (tag.length() > 3 || name.length() > std::size_t(int(character->world->config["GuildMaxNameLength"])))
	{
		return;
	}

	if (IsGuildSessionValid(character, packet.session_id) && !character->guild)
	{
		if (static_cast<int>(character->world->config["GuildCreateMembers"]) > 0)
		{
			std::shared_ptr<Guild> guild(character->world->guildmanager->GetGuild(tag));

			if (!guild)
			{
				guild = character->world->guildmanager->GetGuildName(name);
			}

			if (!guild)
			{
				if (character->world->guildmanager->ValidTag(tag) && character->world->guildmanager->ValidName(name))
				{
					if (character->HasItem(1) >= static_cast<int>(character->world->config["GuildPrice"]))
					{
						std::shared_ptr<class Guild_Create> create = character->world->guildmanager->BeginCreate(tag, name, character);
						character->guild_create = create;

						character->Send(server::GuildReplyServerPacket::ForCreateBegin());

						server::GuildRequestServerPacket invite;
						invite.player_id = create->leader->PlayerID();
						invite.guild_identity = util::ucfirst(util::lowercase(name)) + " (" + util::uppercase(tag) + ")";

						UTIL_FOREACH(character->map->characters, updatecharacter)
						{
							if (updatecharacter != character && !updatecharacter->guild)
							{
								updatecharacter->guild_invite = tag;
								updatecharacter->Send(invite);
							}
						}

						if (static_cast<int>(character->world->config["GuildCreateMembers"]) == 1)
						{
							server::GuildReplyServerPacket::ReplyCodeDataCreateAddConfirm confirm;
							character->Send(server::GuildReplyServerPacket::ForCreateAddConfirm(confirm));
						}
					}
				}
				else
				{
					character->Send(server::GuildReplyServerPacket::ForNotApproved());
				}
			}
			else
			{
				character->Send(server::GuildReplyServerPacket::ForExists());
			}
		}
	}
}

// Accept guild create invite
void Guild_Accept(Character* character, const client::GuildAcceptClientPacket& packet)
{
	if (packet.session_id != client::GuildAcceptClientPacket::DEFAULT_SESSION_ID)
	{
		character->player->client->Close();
		return;
	}

	if (!character->guild_invite.empty())
	{
		std::shared_ptr<class Guild_Create> create = character->world->guildmanager->GetCreate(character->guild_invite);

		if (create)
		{
			create->AddMember(character->real_name);

			if (create->members.size() == static_cast<std::size_t>(static_cast<int>(character->world->config["GuildCreateMembers"])))
			{
				server::GuildReplyServerPacket::ReplyCodeDataCreateAddConfirm confirm;
				confirm.name = character->real_name;
				create->leader->Send(server::GuildReplyServerPacket::ForCreateAddConfirm(confirm));
			}
			else
			{
				server::GuildReplyServerPacket::ReplyCodeDataCreateAdd add;
				add.name = character->real_name;
				create->leader->Send(server::GuildReplyServerPacket::ForCreateAdd(add));
			}
		}

		character->guild_invite = "";
	}
}

// Left the guild
void Guild_Remove(Character* character, const client::GuildRemoveClientPacket& packet)
{
	if (IsGuildSessionValid(character, packet.session_id) && character->guild)
	{
		character->guild->DelMember(character->real_name, 0, true);
	}
}

// Update guild rank list
void Guild_Agree(Character* character, const client::GuildAgreeClientPacket& packet)
{
	if (IsGuildSessionValid(character, packet.session_id) && character->guild)
	{
		switch (packet.info_type)
		{
			case client::GuildInfoType::Description:
			{
				const auto* data = packet.AsDescription();

				if (data && character->guild_rank <= static_cast<int>(character->world->config["GuildEditRank"]))
				{
					if (!character->world->guildmanager->ValidDescription(data->description))
						return;

					character->guild->SetDescription(data->description);

					character->Send(server::GuildReplyServerPacket::ForUpdated());
				}
			}
			break;

			case client::GuildInfoType::Ranks:
			{
				const auto* data = packet.AsRanks();

				if (data && character->guild_rank <= static_cast<int>(character->world->config["GuildEditRank"]))
				{
					if (data->ranks.size() != character->guild->ranks.size())
						return;

					for (const std::string& rank : data->ranks)
					{
						if (!character->world->guildmanager->ValidRank(rank))
							return;
					}

					std::copy(data->ranks.begin(), data->ranks.end(), character->guild->ranks.begin());

					character->guild->needs_save = true;

					character->Send(server::GuildReplyServerPacket::ForRanksUpdated());
				}
			}
			break;

			default: ;
		}
	}
}

// Creating a guild
void Guild_Create(Character* character, const client::GuildCreateClientPacket& packet)
{
	std::string tag = packet.guild_tag;
	std::string name = packet.guild_name;
	std::string description = packet.description;

	if (tag.length() > 3
	 || name.length() > std::size_t(int(character->world->config["GuildMaxNameLength"]))
	 || !character->world->guildmanager->ValidDescription(description))
	{
		return;
	}

	if (IsGuildSessionValid(character, packet.session_id) && !character->guild && character->HasItem(1) >= character->world->config["GuildPrice"].GetInt())
	{
		std::shared_ptr<class Guild_Create> create = character->world->guildmanager->GetCreate(tag);

		if (create && create->leader == character && create->members.size() >= static_cast<std::size_t>(character->world->config["GuildCreateMembers"].GetInt()))
		{
			std::shared_ptr<Guild> guild = character->world->guildmanager->CreateGuild(create, description);

			if (!guild)
				return;

			character->DelItem(1, character->world->config["GuildPrice"]);

			server::GuildCreateServerPacket reply;
			reply.leader_player_id = create->leader->PlayerID();
			reply.guild_tag = guild->tag;
			reply.guild_name = guild->name;
			reply.rank_name = character->GuildRankString();
			reply.gold_amount = character->HasItem(1);
			character->Send(reply);

			character->guild_create.reset();
		}
	}
}

// Requested to join a guild
void Guild_Player(Character* character, const client::GuildPlayerClientPacket& packet)
{
	std::string tag = util::uppercase(packet.guild_tag);
	std::string recruiter_name = util::lowercase(packet.recruiter_name);

	if (tag.length() > 3 || recruiter_name.length() > 12)
	{
		return;
	}

	if (IsGuildSessionValid(character, packet.session_id) && !character->guild)
	{
		Character *recruiter = character->world->GetCharacterReal(recruiter_name);

		if (recruiter)
		{
			if (!recruiter->nowhere && recruiter->mapid == character->mapid)
			{
				if (recruiter->guild && recruiter->guild->tag == tag)
				{
					if (recruiter->guild_rank <= static_cast<int>(character->world->config["GuildRecruitRank"]))
					{
						character->guild_join = tag;

						server::GuildReplyServerPacket::ReplyCodeDataJoinRequest request;
						request.player_id = character->PlayerID();
						request.name = util::ucfirst(character->real_name);
						recruiter->Send(server::GuildReplyServerPacket::ForJoinRequest(request));
					}
					else
					{
						character->Send(server::GuildReplyServerPacket::ForNotRecruiter());
					}
				}
				else
				{
					character->Send(server::GuildReplyServerPacket::ForRecruiterWrongGuild());
				}
			}
			else
			{
				character->Send(server::GuildReplyServerPacket::ForRecruiterNotHere());
			}
		}
		else
		{
			character->Send(server::GuildReplyServerPacket::ForRecruiterOffline());
		}
	}
}

// Requested guild bank/rank list/description
void Guild_Take(Character* character, const client::GuildTakeClientPacket& packet)
{
	if (IsGuildSessionValid(character, packet.session_id) && character->guild)
	{
		switch (packet.info_type)
		{
			case client::GuildInfoType::Description:
			{
				if (character->guild_rank <= static_cast<int>(character->world->config["GuildEditRank"]))
				{
					server::GuildTakeServerPacket reply;
					reply.description = character->guild->description;

					character->Send(reply);
				}
			}
			break;

			case client::GuildInfoType::Ranks:
			{
				if (character->guild_rank <= static_cast<int>(character->world->config["GuildEditRank"]))
				{
					server::GuildRankServerPacket reply;
					reply.ranks.assign(character->guild->ranks.begin(), character->guild->ranks.end());

					character->Send(reply);
				}
			}
			break;

			case client::GuildInfoType::Bank:
			{
				server::GuildSellServerPacket reply;
				reply.gold_amount = character->guild->bank;

				character->Send(reply);
			}
			break;
		}
	}
}

// Accepted a join request
void Guild_Use(Character* character, const client::GuildUseClientPacket& packet)
{
	Character *joiner = character->world->GetCharacterPID(packet.player_id);

	if (character->guild
	 && character->guild_rank <= static_cast<int>(character->world->config["GuildRecruitRank"])
	 && joiner && !joiner->guild
	 && character->map == joiner->map
	 && joiner->guild_join == character->guild->tag)
	{
		if (character->guild->bank >= static_cast<int>(character->world->config["RecruitCost"]))
		{
			if (character->guild->members.size() < static_cast<std::size_t>(static_cast<int>(character->world->config["GuildMaxMembers"])))
			{
				character->guild->AddMember(joiner, character, true);
				character->guild->DelBank(character->world->config["RecruitCost"]);
				character->Send(server::GuildReplyServerPacket::ForAccepted());
			}
		}
		else
		{
			character->Send(server::GuildReplyServerPacket::ForAccountLow());
		}
	}
}

// Deposit gold to the guild bank
void Guild_Buy(Character* character, const client::GuildBuyClientPacket& packet)
{
	if (character->trading) return;
	if (!character->CanInteractItems()) return;

	int gold = packet.gold_amount;

	if (gold < 0)
		return;

	if (IsGuildSessionValid(character, packet.session_id) && character->guild)
	{
		int max_deposit = int(character->world->config["GuildBankMax"]) - character->guild->bank;

		if (max_deposit <= 0)
			return;

		gold = std::min(gold, max_deposit);

		if (gold >= static_cast<int>(character->world->config["GuildMinDeposit"]) && character->HasItem(1) >= gold)
		{
			character->DelItem(1, gold);
			character->guild->AddBank(gold);

			server::GuildBuyServerPacket reply;
			reply.gold_amount = character->HasItem(1);
			character->Send(reply);
		}
	}
}

// Talked to a guild NPC
void Guild_Open(Character* character, const client::GuildOpenClientPacket& packet)
{
	UTIL_FOREACH(character->map->npcs, npc)
	{
		if (npc->index == packet.npc_index && npc->PubData().type == pub::NpcType::Guild)
		{
			character->npc = npc;
			character->npc_type = pub::NpcType::Guild;

			server::GuildOpenServerPacket reply;
			reply.session_id = character->NewNpcSession();

			character->Send(reply);

			break;
		}
	}
}

// Requested member list of a guild
void Guild_Tell(Character* character, const client::GuildTellClientPacket& packet)
{
	std::string tag = packet.guild_identity;

	if (tag.length() > std::size_t(int(character->world->config["GuildMaxNameLength"])))
	{
		return;
	}

	if (IsGuildSessionValid(character, packet.session_id))
	{
		std::shared_ptr<Guild> guild;

		if (tag.length() == 2 || tag.length() == 3)
		{
			guild = character->world->guildmanager->GetGuild(tag);
		}
		else
		{
			guild = character->world->guildmanager->GetGuildName(tag);
		}

		if (!guild)
		{
			character->Send(server::GuildReplyServerPacket::ForNotFound());
		}
		else
		{
			server::GuildTellServerPacket reply;
			reply.members.reserve(guild->members.size());

			std::for_each(UTIL_CRANGE(guild->members), [&](std::shared_ptr<Guild_Member> member)
			{
				server::GuildMember& entry = reply.members.emplace_back();
				entry.rank = member->rank;
				entry.name = member->name;

				if (character->world->config["GuildCustomRanks"] && !member->rank_string.empty())
				{
					entry.rank_name = member->rank_string;
				}
				else
				{
					entry.rank_name = guild->GetRank(member->rank);
				}
			});

			character->Send(reply);
		}
	}
}

// Requested information on a guild
void Guild_Report(Character* character, const client::GuildReportClientPacket& packet)
{
	std::string tag = packet.guild_identity;

	if (tag.length() > std::size_t(int(character->world->config["GuildMaxNameLength"])))
	{
		return;
	}

	if (IsGuildSessionValid(character, packet.session_id))
	{
		std::shared_ptr<Guild> guild;

		if (tag.length() == 2 || tag.length() == 3)
		{
			guild = character->world->guildmanager->GetGuild(tag);
		}
		else
		{
			guild = character->world->guildmanager->GetGuildName(tag);
		}

		if (!guild)
		{
			character->Send(server::GuildReplyServerPacket::ForNotFound());
		}
		else
		{
			int leader_rank = std::max(static_cast<int>(character->world->config["GuildEditRank"]), static_cast<int>(character->world->config["GuildKickRank"]));
			int recruiter_rank = character->world->config["GuildRecruitRank"];

			std::list<std::shared_ptr<Guild_Member>> leaders;
			std::list<std::shared_ptr<Guild_Member>> recruiters;

			UTIL_FOREACH(guild->members, member)
			{
				if (member->rank <= leader_rank)
				{
					leaders.push_back(member);
				}
			}

			if (character->world->config["GuildShowRecruiters"])
			{
				UTIL_FOREACH(guild->members, member)
				{
					if (member->rank > leader_rank && member->rank <= recruiter_rank)
					{
						recruiters.push_back(member);
					}
				}
			}

			std::string create_date;
			create_date.resize(31);

			tm *local_time = localtime(&guild->created);
			create_date = create_date.substr(0, strftime(&create_date[0], 31, static_cast<std::string>(character->world->config["GuildDateFormat"]).c_str(), local_time));

			server::GuildReportServerPacket reply;
			reply.name = guild->name;
			reply.tag = guild->tag;
			reply.create_date = create_date;
			reply.description = guild->description;
			reply.wealth = util::to_string(guild->bank);

			for (std::size_t i = 0; i < guild->ranks.size(); ++i)
			{
				std::string rank = guild->ranks[i];

				while (rank.length() < 4)
				{
					rank += ' ';
				}

				reply.ranks.push_back(rank);
			}

			UTIL_FOREACH(leaders, member)
			{
				server::GuildStaff& staff = reply.staff.emplace_back();
				staff.rank = 1;
				staff.name = member->name + (member->rank == 0 ? " (founder)" : "");
			}

			UTIL_FOREACH(recruiters, member)
			{
				server::GuildStaff& staff = reply.staff.emplace_back();
				staff.rank = 2;
				staff.name = member->name;
			}

			character->Send(reply);
		}
	}
}

// Disband guild
void Guild_Junk(Character* character, const client::GuildJunkClientPacket& packet)
{
	if (IsGuildSessionValid(character, packet.session_id) && character->guild)
	{
		if (character->guild_rank <= static_cast<int>(character->world->config["GuildDisbandRank"]))
		{
			character->guild->Disband(character);
		}
	}
}

// Remove a member
void Guild_Kick(Character* character, const client::GuildKickClientPacket& packet)
{
	std::string name = packet.member_name;

	if (name.length() > 12)
	{
		return;
	}

	if (IsGuildSessionValid(character, packet.session_id) && character->guild)
	{
		if (character->guild_rank <= static_cast<int>(character->world->config["GuildKickRank"]))
		{
			std::shared_ptr<Guild_Member> target = character->guild->GetMember(name);

			if (target)
			{
				if (target->rank > character->guild_rank)
				{
					character->guild->DelMember(name, character, true);
					character->Send(server::GuildReplyServerPacket::ForRemoved());
				}
				else
				{
					character->Send(server::GuildReplyServerPacket::ForRemoveLeader());
				}
			}
			else
			{
				character->Send(server::GuildReplyServerPacket::ForRemoveNotMember());
			}
		}
	}
}

// Set a member's rank
void Guild_Rank(Character* character, const client::GuildRankClientPacket& packet)
{
	int rank = packet.rank;
	std::string name = util::lowercase(packet.member_name);

	if (rank < 0 || rank > 10 || name.length() > 12)
	{
		return;
	}

	if (IsGuildSessionValid(character, packet.session_id) && character->guild)
	{
		std::shared_ptr<Guild_Member> target = character->guild->GetMember(name);

		if (target)
		{
			if (target->rank == 0)
			{
				character->Send(server::GuildReplyServerPacket::ForRankingLeader());
			}
			else if (rank == 0)
			{
				if (character->guild_rank == 0 && character->world->config["GuildMultipleFounders"])
				{
					character->guild->SetMemberRank(target->name, 0);
					character->Send(server::GuildReplyServerPacket::ForUpdated());
				}
			}
			else
			{
				bool has_perm = false;

				if (rank <= target->rank)
				{
					has_perm = has_perm || (character->guild_rank <= int(character->world->config["GuildPromoteRank"]));
				}
				else if (rank >= target->rank)
				{
					has_perm = has_perm || (character->guild_rank <= int(character->world->config["GuildDemoteRank"]));
				}

				if (has_perm)
				{
					if (character->guild_rank == 0 || character->guild_rank < target->rank )
					{
						if (rank > character->guild_rank
						 || (character->guild_rank <= int(character->world->config["GuildPromoteSameRank"]) && character->guild_rank <= rank))
						{
							character->guild->SetMemberRank(target->name, rank);
							character->Send(server::GuildReplyServerPacket::ForUpdated());
						}
					}
					else
					{
						character->Send(server::GuildReplyServerPacket::ForRankingLeader());
					}
				}
			}
		}
		else
		{
			character->Send(server::GuildReplyServerPacket::ForRankingNotMember());
		}
	}
}

PACKET_HANDLER_REGISTER(PACKET_GUILD)
	Register(Guild_Request, Playing, 1.0);
	Register(Guild_Accept, Playing);
	Register(Guild_Remove, Playing);
	Register(Guild_Agree, Playing);
	Register(Guild_Create, Playing, 1.0);
	Register(Guild_Player, Playing, 0.5);
	Register(Guild_Take, Playing);
	Register(Guild_Use, Playing);
	Register(Guild_Buy, Playing);
	Register(Guild_Open, Playing);
	Register(Guild_Tell, Playing, 0.5);
	Register(Guild_Report, Playing, 0.5);
	Register(Guild_Junk, Playing);
	Register(Guild_Kick, Playing);
	Register(Guild_Rank, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_GUILD)

}
