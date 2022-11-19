
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "commands.hpp"

#include "../character.hpp"
#include "../command_source.hpp"
#include "../config.hpp"
#include "../eoserver.hpp"
#include "../map.hpp"
#include "../timer.hpp"
#include "../world.hpp"

#include "../console.hpp"
#include "../util.hpp"

#include <csignal>
#include <string>
#include <vector>

volatile std::sig_atomic_t eoserv_sig_abort = false;
volatile std::sig_atomic_t eoserv_sig_reload = false;

TimeEvent* shutdown_timer = nullptr;

static void schedule_shutdown(const std::vector<std::string>& arguments, Command_Source* from, bool is_reload)
{
	if (shutdown_timer != nullptr)
	{
		from->ServerMsg(from->SourceWorld()->i18n.Format("server_shutdown_in_progress"));
		return;
	}

	int timeout = 0;
	if (arguments.size() >= 1)
	{
		if (util::lowercase(arguments[0]) != "now")
		{
			timeout = util::variant(arguments[0]).GetInt();
			if (timeout > 0)
			{
				from->SourceWorld()->ServerMsg(from->SourceWorld()->i18n.Format("server_shutdown_scheduled", is_reload ? "reloaded" : "shut down", timeout));
			}
			else
			{
				timeout = 0;
			}
		}
	}

	// capturing lambdas cannot be passed as function pointers, therefore is_reload cannot be captured
	TimerCallback shutdown_callback = nullptr;
	if (is_reload)
	{
		shutdown_callback = [](void*)
		{
			Console::Out("Server reloaded");
			eoserv_sig_reload = true;
		};
	}
	else
	{
		shutdown_callback = [](void*)
		{
			Console::Out("Server shut down");
			eoserv_sig_abort = true;
		};
	}

	shutdown_timer = new TimeEvent(shutdown_callback, nullptr, timeout);
	from->SourceWorld()->timer.Register(shutdown_timer);
}

namespace Commands
{

void ReloadMap(const std::vector<std::string>& arguments, Character* from)
{
	World* world = from->SourceWorld();
	Map* map = from->map;
	bool isnew = false;

	if (arguments.size() >= 1)
	{
		int mapid = util::to_int(arguments[0]);

		if (mapid < 1)
			mapid = 1;

		if (world->maps.size() > static_cast<size_t>(mapid - 1))
		{
			map = world->maps[mapid - 1];
		}
		else if (mapid <= static_cast<int>(world->config["Maps"]))
		{
			isnew = true;

			while (world->maps.size() < static_cast<size_t>(mapid))
			{
				int newmapid = world->maps.size() + 1;
				world->maps.push_back(new Map(newmapid, world));
			}
		}
	}

	if (map && !isnew)
	{
		map->Reload();
	}
}

void ReloadPub(const std::vector<std::string>& arguments, Command_Source* from)
{
	(void)arguments;

	Console::Out("Pub files reloaded by %s", from->SourceName().c_str());

	bool quiet = true;

	if (arguments.size() >= 1)
		quiet = (arguments[0] != "announce");

	from->SourceWorld()->ReloadPub(quiet);
}

void ReloadConfig(const std::vector<std::string>& arguments, Command_Source* from)
{
	(void)arguments;

	Console::Out("Config reloaded by %s", from->SourceName().c_str());
	from->SourceWorld()->Rehash();
}

void ReloadQuest(const std::vector<std::string>& arguments, Command_Source* from)
{
	(void)arguments;

	Console::Out("Quests reloaded by %s", from->SourceName().c_str());
	from->SourceWorld()->ReloadQuests();
}

void Shutdown(const std::vector<std::string>& arguments, Command_Source* from)
{
	schedule_shutdown(arguments, from, false);
}

void Reload(const std::vector<std::string>& arguments, Command_Source* from)
{
	schedule_shutdown(arguments, from, true);
}

void Cancel(const std::vector<std::string>& arguments, Command_Source* from)
{
	(void)arguments;

	if (shutdown_timer == nullptr)
	{
		from->ServerMsg(from->SourceWorld()->i18n.Format("server_no_shutdown_in_progress"));
		return;
	}

	from->SourceWorld()->timer.Unregister(shutdown_timer);
	delete shutdown_timer;
	shutdown_timer = nullptr;

	from->SourceWorld()->ServerMsg(from->SourceWorld()->i18n.Format("server_shutdown_cancelled"));
}

void Uptime(const std::vector<std::string>& arguments, Command_Source* from)
{
	(void)arguments;

	std::string buffer = "Server started ";
	buffer += util::timeago(from->SourceWorld()->server->start, Timer::GetTime());
	from->ServerMsg(buffer);
}

COMMAND_HANDLER_REGISTER(server)
	RegisterCharacter({"remap", {}, {"mapid"}, 3}, ReloadMap);
	Register({"repub", {}, {"announce"}, 3}, ReloadPub);
	Register({"rehash"}, ReloadConfig);
	Register({"request", {}, {}, 3}, ReloadQuest);
	Register({"shutdown", {}, {}, 8}, Shutdown);
	Register({"reload", {}, {}, 6}, Reload);
	Register({"cancel", {}, {}, 6}, Cancel);
	Register({"uptime"}, Uptime);
COMMAND_HANDLER_REGISTER_END(server)

}
