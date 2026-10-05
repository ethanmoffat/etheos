/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "eodata_file.hpp"

#include "console.hpp"

#include <eolib/data/eo_reader.hpp>
#include <eolib/data/eo_writer.hpp>
#include <eolib/errors.hpp>

#include <fstream>
#include <iterator>

namespace EODataFile
{

bool Read(const std::string& filename, std::vector<std::uint8_t>& content)
{
	std::ifstream file(filename, std::ios::binary);

	if (!file)
		return false;

	content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

	return !file.bad();
}

void Deserialize(const std::string& filename, const std::vector<std::uint8_t>& content, eolib::protocol::Serializable& object)
{
	auto reader = eolib::data::EoReader::View(content);
	object.Deserialize(reader);

	eolib::data::EoWriter writer;

	try
	{
		object.Serialize(writer);
	}
	catch (const eolib::SerializationError& e)
	{
		Console::Wrn("File may be invalid: %s (%s)", filename.c_str(), e.what());
		return;
	}

	if (static_cast<std::size_t>(writer.Length()) != content.size())
	{
		Console::Wrn("File may be invalid: %s (read %zu bytes, expected %i)", filename.c_str(), content.size(), writer.Length());
	}
}

}
