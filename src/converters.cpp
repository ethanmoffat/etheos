/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "converters.hpp"

#include "character.hpp"

namespace Converters
{

server::CharacterSelectionListEntry CharacterSelectionListEntryFrom(Character* character)
{
	const Character::PaperdollGraphics graphics = character->GetPaperdollGraphics();

	server::CharacterSelectionListEntry entry;
	entry.name = character->SourceName();
	entry.id = character->id;
	entry.level = character->level;
	entry.gender = static_cast<eolib::protocol::Gender>(character->gender);
	entry.hair_style = character->hairstyle;
	entry.hair_color = character->haircolor;
	entry.skin = character->race;
	entry.admin = static_cast<eolib::protocol::AdminLevel>(character->admin);
	entry.equipment.boots = graphics.boots;
	entry.equipment.armor = graphics.armor;
	entry.equipment.hat = graphics.hat;
	entry.equipment.shield = graphics.shield;
	entry.equipment.weapon = graphics.weapon;
	return entry;
}

}
