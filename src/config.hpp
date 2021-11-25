
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef CONFIG_HPP_INCLUDED
#define CONFIG_HPP_INCLUDED

#include "fwd/config.hpp"

#include "util/variant.hpp"

#include <cstddef>
#include <string>
#include <unordered_map>

/**
 * Reads configuration data from a file.
 * Does not support sections in files (they're usually ignored).
 */
class Config : public std::unordered_map<std::string, util::variant>
{
	private:
		void LoadFromEnvironment(const std::string& key);

	protected:
		/**
		 * Filename of the configuration file.
		 * Stored in case save support is ever added.
		 */
		std::string filename;

		std::list<std::string> loadedEnvs;

	public:
		/**
		 * Maximum length of a line in the configuration file.
		 */
		static const std::size_t MaxLineLength = 4096;

		/**
		 * Construct an empty Config object which should have Read() called on it
		 */
		Config() { }

		/**
		 * Reads all configuration data from the file to memory.
		 * @param filename File to read from.
		 */
		Config(const std::string& filename) { Read(filename); }

		/**
		 * Reads all configuration data from the file to memory.
		 * @param filename File to read from.
		 */
		void Read(const std::string& filename);

		util::variant& operator[](const std::string& key);
		util::variant& operator[](std::string&& key);
};

#endif // CONFIG_HPP_INCLUDED
