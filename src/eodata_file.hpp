/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#pragma once

#include <eolib/protocol/serializable.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace EODataFile
{
	bool Read(const std::string& filename, std::vector<std::uint8_t>& content);

	/**
	 * Deserializes file content into an eolib object. Logs a warning if serializing the result doesn't give back the
	 * file's size, which means the file is truncated, has trailing data or has values outside the format's limits.
	 */
	void Deserialize(const std::string& filename, const std::vector<std::uint8_t>& content,
		eolib::protocol::Serializable& object);
}
