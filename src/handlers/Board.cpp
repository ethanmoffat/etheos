
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../map.hpp"
#include "../timer.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <eolib/protocol/net/client/packets.hpp>
#include <eolib/protocol/net/server/packets.hpp>

#include <algorithm>
#include <cstddef>
#include <string>

namespace Handlers
{

// Removing a post from a town board
void Board_Remove(Character* character, const client::BoardRemoveClientPacket& packet)
{
	if (character->board)
	{
		if (character->SourceAccess() < character->world->AdminLevelSetting("boardmod"))
		{
			// Not in the official EO servers, but nice to use
			character->ShowBoard();
			return;
		}

		UTIL_IFOREACH(character->board->posts, post)
		{
			if ((*post)->id == packet.post_id)
			{
				if ((*post)->author_admin < character->admin
				 || (*post)->author == character->SourceName())
				{
					delete *post;
					character->board->posts.erase(post);
				}
				break;
			}
		}

		// Not in the official EO servers, but nice to use
		character->ShowBoard();
	}
}

// Posting to a message board
void Board_Create(Character* character, const client::BoardCreateClientPacket& packet)
{
	const std::string subject = packet.post_subject.substr(0, static_cast<int>(character->world->config["BoardMaxSubjectLength"]));
	const std::string body = packet.post_body.substr(0, static_cast<int>(character->world->config["BoardMaxPostLength"]));

	int post_count = 0;
	int recent_post_count = 0;

	if (character->board)
	{
		UTIL_FOREACH(character->board->posts, post)
		{
			if (post->author == character->SourceName())
			{
				++post_count;

				if (post_count >= static_cast<int>(character->world->config["BoardMaxUserPosts"]))
				{
					// Not in the official EO servers, but nice to use
					character->ShowBoard();
					return;
				}

				if (post->time + static_cast<int>(character->world->config["BoardRecentPostTime"]) > Timer::GetTime())
				{
					++recent_post_count;

					if (recent_post_count >= static_cast<int>(character->world->config["BoardMaxUserRecentPosts"]))
					{
						// Not in the official EO servers, but nice to use
						character->ShowBoard();
						return;
					}
				}
			}
		}

		Board_Post *newpost = new Board_Post;
		newpost->id = ++character->board->last_id;
		newpost->author = character->SourceName();
		newpost->author_admin = character->admin;
		newpost->subject = subject;
		newpost->body = body;
		newpost->time = Timer::GetTime();

		character->board->posts.push_front(newpost);

		if (character->board->id == static_cast<int>(character->world->config["AdminBoard"]))
		{
			if (character->board->posts.size() > static_cast<std::size_t>(static_cast<int>(character->world->config["AdminBoardLimit"])))
			{
				character->board->posts.pop_back();
			}
		}
		else
		{
			if (character->board->posts.size() > static_cast<std::size_t>(static_cast<int>(character->world->config["BoardMaxPosts"])))
			{
				character->board->posts.pop_back();
			}
		}

		// Not in the official EO servers, but nice to use
		character->ShowBoard();
	}
}

// Reading a post on a town board
void Board_Take(Character* character, const client::BoardTakeClientPacket& packet)
{
	if (!character->board) return;

	auto post = std::find_if(UTIL_RANGE(character->board->posts), [&packet](const Board_Post* post) { return post->id == packet.post_id; });

	if (post == character->board->posts.end())
		return;

	server::BoardPlayerServerPacket reply;
	reply.post_id = packet.post_id;
	reply.post_body = (*post)->body;
	character->Send(reply);
}

// Opening town board
void Board_Open(Character* character, const client::BoardOpenClientPacket& packet)
{
	const int boardid = packet.board_id;

	if (static_cast<std::size_t>(boardid) >= character->world->boards.size())
	{
		return;
	}

	if (boardid != static_cast<int>(character->world->config["AdminBoard"]) - 1
	 || character->SourceAccess() >= character->world->AdminLevelSetting("reports"))
	{
		for (std::size_t y = 0; y < character->map->height; ++y)
		{
			for (std::size_t x = 0; x < character->map->width; ++x)
			{
				auto xChar = static_cast<unsigned char>(x);
				auto yChar = static_cast<unsigned char>(y);
				if (character->InRange(xChar, yChar)
				 && character->map->GetSpec(xChar, yChar) == static_cast<emf::MapTileSpec>(static_cast<int>(emf::MapTileSpec::Board1) + boardid))
				{
					character->board = character->world->boards[boardid];
				}
			}
		}
	}

	if (character->board)
	{
		character->ShowBoard();
	}
}

PACKET_HANDLER_REGISTER(PACKET_BOARD)
	Register(Board_Remove, Playing);
	Register(Board_Create, Playing);
	Register(Board_Take, Playing);
	Register(Board_Open, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_BOARD)

}
