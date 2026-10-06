/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include "fwd/character.hpp"
#include "fwd/map.hpp"
#include "fwd/npc.hpp"

#include <eolib/protocol/net/server/packets.hpp>
#include <eolib/protocol/net/server/structs.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace Converters
{
	namespace server = eolib::protocol::net::server;

	constexpr std::size_t NewsLineCount = 9;

	server::BigCoords BigCoordsFrom(const eolib::protocol::Coords& coords);

	server::CharacterSelectionListEntry CharacterSelectionListEntryFrom(Character* character);

	server::CharacterMapInfo CharacterMapInfoFrom(Character* character);

	server::NpcMapInfo NpcMapInfoFrom(const NPC* npc);

	server::ItemMapInfo ItemMapInfoFrom(const Map_Item& item);

	/**
	 * Creates the list of characters, living NPCs and items on the map that are in range of viewer.
	 */
	server::NearbyInfo NearbyInfoFrom(Character* viewer);

	/**
	 * Creates the change that shows a character's current equipment to other players.
	 */
	server::AvatarChange EquipmentChangeFrom(Character* character);

	/**
	 * Creates the reply to a player selecting a character: the character's details and the revisions of the files the
	 * client needs. login_warning tells the client to show the first login message.
	 */
	server::WelcomeReplyServerPacket::WelcomeCodeDataSelectCharacter SelectCharacterFrom(Character* character, bool login_warning);

	/**
	 * Creates the reply that enters a character into the game: news lines, the character's inventory and spells, and
	 * what's nearby on the map. News is padded with empty lines, or cut, to NewsLineCount.
	 */
	server::WelcomeReplyServerPacket::WelcomeCodeDataEnterGame EnterGameFrom(Character* character, std::vector<std::string> news, server::NearbyInfo nearby);
}
