
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../i18n.hpp"
#include "../world.hpp"

#include <eolib/protocol/net/client/packets.hpp>

#include <string>

namespace Handlers
{

// "Talk to admin" message
void AdminInteract_Tell(Character* character, const client::AdminInteractTellClientPacket& packet)
{
	std::string message = packet.message;

	if (character->world->config["OldReports"])
	{
		message = character->world->i18n.Format("admin_request", message);
		character->world->AdminMsg(character, message, character->world->AdminLevelSetting("reports"));
	}
	else
	{
		character->world->AdminRequest(character, message);
	}
}

// User report
void AdminInteract_Report(Character* character, const client::AdminInteractReportClientPacket& packet)
{
	const std::string& reportee = packet.reportee;
	std::string message = packet.message;

	if (character->world->config["OldReports"])
	{
		message = character->world->i18n.Format("admin_report", reportee, message);
		character->world->AdminMsg(character, message, character->world->AdminLevelSetting("reports"));
	}
	else
	{
		character->world->AdminReport(character, reportee, message);
	}
}

PACKET_HANDLER_REGISTER(AdminInteract)
	Register(AdminInteract_Tell, Playing | OutOfBand);
	Register(AdminInteract_Report, Playing | OutOfBand);
PACKET_HANDLER_REGISTER_END(AdminInteract)

}
