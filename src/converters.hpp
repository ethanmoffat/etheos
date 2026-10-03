/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include "fwd/character.hpp"

#include <eolib/protocol/net/server/structs.hpp>

namespace Converters
{
	namespace server = eolib::protocol::net::server;

	server::CharacterSelectionListEntry CharacterSelectionListEntryFrom(Character* character);
}
