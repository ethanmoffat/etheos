
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
	if (shutdown_timer != nullptr)
	{
		from->ServerMsg("Shutdown/reload is already in progress. Use $cancel to cancel pending shutdown/reload.");
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
				from->SourceWorld()->ServerMsg(std::string("Attention!! Server will be shut down in ") + util::to_string(timeout) + std::string(" seconds"));
			}
			else
			{
				timeout = 0;
			}
		}
	}

	shutdown_timer = new TimeEvent([](void * input)
	{
		Command_Source* input_from = static_cast<Command_Source*>(input);
		Console::Wrn("Server shut down by %s", input_from->SourceName().c_str());
		eoserv_sig_abort = true;
	}, from, timeout);

	from->SourceWorld()->timer.Register(shutdown_timer);
}

void Reload(const std::vector<std::string>& arguments, Command_Source* from)
{
	if (shutdown_timer != nullptr)
	{
		from->ServerMsg("Shutdown/reload is already in progress. Use $cancel to cancel pending shutdown/reload.");
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
				from->SourceWorld()->ServerMsg(std::string("Attention!! Server will be reloaded in ") + util::to_string(timeout) + std::string(" seconds"));
			}
			else
			{
				timeout = 0;
			}
		}
	}

	shutdown_timer = new TimeEvent([](void * input)
	{
		Command_Source* input_from = static_cast<Command_Source*>(input);
		Console::Wrn("Server reloaded by %s", input_from->SourceName().c_str());
		eoserv_sig_reload = true;
	}, from, timeout * 1000.0);

	from->SourceWorld()->timer.Register(shutdown_timer);
}

void Cancel(const std::vector<std::string>& arguments, Command_Source* from)
{
	if (shutdown_timer == nullptr)
	{
		from->ServerMsg("No shutdown/reload is in progress. Use $shutdown [timeout_seconds] or $reload [timeout_seconds] to schedule.");
		return;
	}

	from->SourceWorld()->timer.Unregister(shutdown_timer);
	delete shutdown_timer;
	shutdown_timer = nullptr;

	from->SourceWorld()->ServerMsg("Attention!! Server shutdown was cancelled.");
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
