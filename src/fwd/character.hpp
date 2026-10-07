
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef FWD_CHARACTER_HPP_INCLUDED
#define FWD_CHARACTER_HPP_INCLUDED

#include <eolib/protocol/enums.hpp>
#include <eolib/protocol/net/server/enums.hpp>

class Character;

struct Character_QuestState;

using Direction = eolib::protocol::Direction;
using Emote = eolib::protocol::Emote;
using SitState = eolib::protocol::net::server::SitState;
using AvatarChangeType = eolib::protocol::net::server::AvatarChangeType;

enum QuestPage : unsigned char
{
	QUEST_PAGE_PROGRESS = 1,
	QUEST_PAGE_HISTORY = 2
};

enum BookIcon : short
{
    BOOK_ICON_ITEM = 3,
    BOOK_ICON_TALK = 5,
    BOOK_ICON_KILL = 8,
    BOOK_ICON_STEP = 10,
};

#endif // FWD_CHARACTER_HPP_INCLUDED
