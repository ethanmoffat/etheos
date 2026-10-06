
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef FWD_CHARACTER_HPP_INCLUDED
#define FWD_CHARACTER_HPP_INCLUDED

#include <eolib/protocol/enums.hpp>
#include <eolib/protocol/net/server/enums.hpp>

class Character;

struct Character_Item;
struct Character_Spell;
struct Character_QuestState;

enum AdminLevel : unsigned char
{
	ADMIN_PLAYER = 0,
	ADMIN_GUIDE = 1,
	ADMIN_GUARDIAN = 2,
	ADMIN_GM = 3,
	ADMIN_HGM = 4
};

using Direction = eolib::protocol::Direction;
using Emote = eolib::protocol::Emote;
using SitState = eolib::protocol::net::server::SitState;
using AvatarChangeType = eolib::protocol::net::server::AvatarChangeType;

enum QuestPage : unsigned char
{
	QUEST_PAGE_PROGRESS = 1,
	QUEST_PAGE_HISTORY = 2
};

enum Gender : unsigned char
{
	GENDER_FEMALE = 0,
	GENDER_MALE = 1
};

enum Skin : unsigned char
{
	SKIN_WHITE = 0,
	SKIN_TAN = 1,
	SKIN_PALE = 2,
	SKIN_ORC = 3,
	SKIN_SKELETON = 4,
	SKIN_PANDA = 5,
	SKIN_FISH = 6
};

enum PaperdollIcon : unsigned char
{
	ICON_NORMAL = 0,
	ICON_GM = 4,
	ICON_HGM = 5,
	ICON_PARTY = 6,
	ICON_GM_PARTY = 9,
	ICON_HGM_PARTY = 10,
	ICON_SLN_BOT = 20
};

enum TalkReply : short
{
	TALK_NOTFOUND = 1
};

enum TrainType : unsigned char
{
    TRAIN_STAT = 1,
    TRAIN_SKILL = 2
};

enum BookIcon : short
{
    BOOK_ICON_ITEM = 3,
    BOOK_ICON_TALK = 5,
    BOOK_ICON_KILL = 8,
    BOOK_ICON_STEP = 10,
};

#endif // FWD_CHARACTER_HPP_INCLUDED
