
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../converters.hpp"
#include "../dialog.hpp"
#include "../eodata.hpp"
#include "../eoplus.hpp"
#include "../npc.hpp"
#include "../quest.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <cstddef>
#include <deque>
#include <memory>
#include <utility>

namespace Handlers
{

static void open_quest_dialog(Character* character, NPC* npc, int quest_id, int vendor_id)
{
	struct dialog_t
	{
		int quest_id;
		const std::shared_ptr<Quest_Context> quest;
		const Dialog* dialog;
	};

	std::deque<dialog_t> dialogs;
	std::size_t this_dialog = 0;

	character->NewNpcSession();

	UTIL_FOREACH(character->quests, quest)
	{
		if (!quest.second || quest.second->GetQuest()->Disabled())
			continue;

		const Dialog* dialog = quest.second->GetDialog(vendor_id);

		if (dialog)
		{
			dialogs.push_back(dialog_t{quest.first, quest.second, dialog});

			if (quest.first == quest_id)
				this_dialog = dialogs.size() - 1;
		}
	}

	if (!dialogs.empty())
	{
		character->npc = npc;
		character->npc_type = pub::NpcType::Quest;

		server::QuestDialogServerPacket reply;
		reply.behavior_id = vendor_id;
		reply.quest_id = dialogs[this_dialog].quest_id;
		reply.session_id = character->npc_session_id;
		reply.dialog_id = 0;

		for (const dialog_t& dialog : dialogs)
		{
			server::DialogQuestEntry& entry = reply.quest_entries.emplace_back();
			entry.quest_id = dialog.quest_id;
			entry.quest_name = dialog.quest->GetQuest()->Name();
		}

		reply.dialog_entries = Converters::DialogEntriesFrom(*dialogs[this_dialog].dialog);

		character->Send(reply);
	}
}

// Talking to a quest NPC
void Quest_Use(Character* character, const client::QuestUseClientPacket& packet)
{
	int quest_id = packet.quest_id;

	UTIL_FOREACH(character->map->npcs, npc)
	{
		if (npc->index == packet.npc_index && npc->PubData().type == pub::NpcType::Quest && character->InRange(npc))
		{
			short vendor_id = npc->PubData().behavior_id;

			if (vendor_id < 1)
				continue;

			if (quest_id == 0)
				quest_id = vendor_id;

			auto context = character->GetQuest(vendor_id);

			if (!context)
			{
				auto it = character->world->quests.find(vendor_id);

				if (it != character->world->quests.end())
				{
					// WARNING: holds a non-tracked reference to shared_ptr
					Quest* quest = it->second.get();

					if (quest->Disabled())
						continue;

					auto newcontext = std::make_shared<Quest_Context>(character, quest);
					character->quests[it->first] = newcontext;
					newcontext->SetState("begin");
				}
			}
			else if (context->StateName() == "done")
			{
				context->SetState("begin");
			}

			open_quest_dialog(character, npc, quest_id, vendor_id);

			break;
		}
	}
}

// Response to an NPC dialog
void Quest_Accept(Character* character, const client::QuestAcceptClientPacket& packet)
{
	const int quest_id = packet.quest_id;
	char action = 0;

	if (const auto* link = packet.AsLink())
		action = static_cast<char>(link->action);

	if (character->npc_type == pub::NpcType::Quest)
	{
		if (character->npc_session_id != packet.session_id)
			return;

		short vendor_id = character->npc->PubData().behavior_id;
		std::shared_ptr<Quest_Context> quest;

		auto it = character->quests.find(quest_id);

		if (it != character->quests.end() && it->second)
		{
			// WARNING: holds a non-tracked reference to shared_ptr
			const Dialog* dialog = it->second->GetDialog(vendor_id);

			if (dialog && (packet.reply_type == client::DialogReply::Ok || (packet.reply_type == client::DialogReply::Link && dialog->CheckLink(action))))
			{
				quest = it->second;
			}
		}

		if (quest && !quest->GetQuest()->Disabled())
		{
			bool result = action ? quest->DialogInput(action) : quest->TalkedNPC(static_cast<char>(vendor_id));

			// Run dialog for next quest state
			quest = character->GetQuest(quest_id);

			if (result && quest && !quest->GetQuest()->Disabled() && !quest->Finished() && character->npc)
			{
				open_quest_dialog(character, character->npc, quest_id, vendor_id);
			}
		}
	}
}

// Quest history/progress request
void Quest_List(Character* character, const client::QuestListClientPacket& packet)
{
	server::QuestListServerPacket reply;

	switch (packet.page)
	{
		case net::QuestPage::Progress:
		{
			server::QuestListServerPacket::PageDataProgress progress;
			progress.quest_progress_entries = Converters::QuestProgressEntriesFrom(character);
			reply = server::QuestListServerPacket::ForProgress(std::move(progress));
			break;
		}

		case net::QuestPage::History:
		{
			server::QuestListServerPacket::PageDataHistory history;
			history.completed_quests = Converters::FinishedQuestNamesFrom(character);
			reply = server::QuestListServerPacket::ForHistory(std::move(history));
			break;
		}

		default:
			return;
	}

	reply.quests_count = static_cast<int>(character->quests.size());
	character->Send(reply);
}

PACKET_HANDLER_REGISTER(Quest)
	Register(Quest_Use, Playing);
	Register(Quest_Accept, Playing);
	Register(Quest_List, Playing);
PACKET_HANDLER_REGISTER_END(Quest)

}
