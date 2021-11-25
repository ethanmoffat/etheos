
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "config.hpp"

#include "console.hpp"
#include "util.hpp"
#include "util/variant.hpp"

#include <cstddef>
#include <cstdio>
#include <stdexcept>
#include <string>

static std::string eoserv_config_fromenv(const char* key)
{
	std::string envKey("etheos_");
	envKey += key;
	std::transform(envKey.begin(), envKey.end(), envKey.begin(), ::toupper);

	auto envVal = getenv(envKey.c_str());
	return std::string(envVal ? envVal : "");
}

void Config::Read(const std::string& filename)
{
	std::FILE *fh;
	char buf[Config::MaxLineLength];
	std::string line;
	std::string key;
	std::string val;
	std::size_t eqloc;

	this->loadedEnvs.clear();
	this->filename = filename;

	fh = std::fopen(filename.c_str(), "rt");
	if (!fh)
	{
		std::string err = "Configuration file not found: " + filename;
		throw std::runtime_error(err);
	}

	while (!std::feof(fh))
	{
		if (std::fgets(buf, Config::MaxLineLength, fh))
		{
			line = util::trim(buf);
		}
		else
		{
			line = "";
		}

		if (line.length() < 1)
		{
			continue;
		}

		if (line[0] == '#')
		{
			continue;
		}

		eqloc = line.find('=');

		if (eqloc == std::string::npos)
		{
			continue;
		}

		key = util::rtrim(line.substr(0, eqloc));

		if (line.length() > eqloc+1)
		{
			val = util::ltrim(line.substr(eqloc+1));
		}
		else
		{
			val = std::string("");
		}

		if (key == "REQUIRE")
		{
			this->Read(val);
		}
		else if (key == "INCLUDE" || key == "INCLUDE_NOWARN")
		{
			try
			{
				this->Read(val);
			}
			catch (std::runtime_error &e)
			{
				(void)e;
#ifndef DEBUG
				if (key != "INCLUDE_NOWARN")
				{
#endif
					if (key == "INCLUDE_NOWARN")
						Console::Dbg("%s'd configuration file not found: %s", key.c_str(), val.c_str());
					else
						Console::Wrn("%s'd configuration file not found: %s", key.c_str(), val.c_str());
#ifndef DEBUG
				}
#endif
			}
		}
		else
		{
			std::size_t loc = val.find('\\');
			while (loc != std::string::npos && loc != val.length())
			{
				if (val[loc + 1] == 't')
				{
					val.replace(loc, 2, "\t");
				}
				else if (val[loc + 1] == 'r')
				{
					val.replace(loc, 2, "\r");
				}
				else if (val[loc + 1] == 'n')
				{
					val.replace(loc, 2, "\n");
				}
				else if (val[loc + 1] == '\\')
				{
					val.replace(loc, 2, "\\");
				}

				loc = val.find('\\', loc+1);
			}

			this->operator[](key) = static_cast<util::variant>(val);
		}
	}

	std::fclose(fh);
}

util::variant& Config::operator[](std::string&& key)
{
	auto movedKey = std::move(key);
	LoadFromEnvironment(movedKey);
	return unordered_map::operator[](std::move(movedKey));
}

util::variant& Config::operator[](const std::string& key)
{
	LoadFromEnvironment(key);
	return unordered_map::operator[](key);
}

void Config::LoadFromEnvironment(const std::string& key)
{
	if (std::find(loadedEnvs.begin(), loadedEnvs.end(), key) != loadedEnvs.end())
		return;

	std::string envVal = eoserv_config_fromenv(key.c_str());
	if (envVal.length() != 0)
	{
		unordered_map::operator[](key) = util::variant(envVal);
	}

	loadedEnvs.push_back(key);
}
