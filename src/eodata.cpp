/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "eodata.hpp"

#include "console.hpp"
#include "eodata_file.hpp"

#include <eolib/protocol/pub/structs.hpp>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace pub = eolib::protocol::pub;

// File type, rid, record count and version
static constexpr std::size_t pub_header_size = 10;

template <typename TPub> static TPub ReadPub(const std::string& filename)
{
	std::vector<std::uint8_t> content;

	if (!EODataFile::Read(filename, content))
	{
		Console::Err("Could not load file: %s", filename.c_str());
		std::exit(1);
	}

	if (content.size() < pub_header_size)
	{
		Console::Err("Invalid file: %s", filename.c_str());
		std::exit(1);
	}

	TPub file;
	EODataFile::Deserialize(filename, content, file);
	return file;
}

// Converts each record into data, after the empty placeholder at data[0] that Get returns for an unknown id. Ids
// start at 1. A trailing record named "eof" is the end-of-file marker some editors write, and isn't loaded.
template <typename TData, typename TRecord, typename TConvert>
static void LoadRecords(const std::string& filename, int length, const std::vector<TRecord>& records,
	std::vector<TData>& data, TConvert convert)
{
	if (static_cast<int>(records.size()) != length)
	{
		Console::Wrn("File may be invalid: %s (has %zu records, header says %i)", filename.c_str(), records.size(),
			length);
	}

	data.clear();
	data.resize(records.size() + 1);

	for (std::size_t i = 0; i < records.size(); ++i)
	{
		TData& newdata = data[i + 1];
		newdata.id = static_cast<int>(i + 1);
		newdata.name = records[i].name;
		convert(records[i], newdata);
	}

	if (data.size() > 1 && data.back().name == "eof")
	{
		data.pop_back();
	}
}

void EIF::Read(const std::string& filename)
{
	const auto file = ReadPub<pub::Eif>(filename);
	this->rid = file.rid;
	this->length = file.total_items_count;

	LoadRecords(filename, this->length, file.items, this->data, [](const pub::EifRecord& record, EIF_Data& newdata)
	{
		newdata.graphic = record.graphic_id;
		newdata.type = static_cast<EIF::Type>(record.type);
		newdata.subtype = static_cast<EIF::SubType>(record.subtype);
		// Ranged gun hack
		if (newdata.id == 365 && newdata.name == "Gun")
		{
			newdata.subtype = EIF::Ranged;
		}
		// / Ranged gun hack
		newdata.special = static_cast<EIF::Special>(record.special);
		newdata.hp = record.hp;
		newdata.tp = record.tp;
		newdata.mindam = record.min_damage;
		newdata.maxdam = record.max_damage;
		newdata.accuracy = record.accuracy;
		newdata.evade = record.evade;
		newdata.armor = record.armor;

		newdata.str = record.str;
		newdata.intl = record.intl;
		newdata.wis = record.wis;
		newdata.agi = record.agi;
		newdata.con = record.con;
		newdata.cha = record.cha;

		newdata.light = record.light_resistance;
		newdata.dark = record.dark_resistance;
		newdata.earth = record.earth_resistance;
		newdata.air = record.air_resistance;
		newdata.water = record.water_resistance;
		newdata.fire = record.fire_resistance;

		newdata.scrollmap = record.spec1;
		newdata.scrollx = record.spec2;
		newdata.scrolly = record.spec3;

		newdata.levelreq = record.level_requirement;
		newdata.classreq = record.class_requirement;

		newdata.strreq = record.str_requirement;
		newdata.intreq = record.int_requirement;
		newdata.wisreq = record.wis_requirement;
		newdata.agireq = record.agi_requirement;
		newdata.conreq = record.con_requirement;
		newdata.chareq = record.cha_requirement;

		newdata.weight = record.weight;

		newdata.size = static_cast<EIF::Size>(record.size);
	});

	Console::Out("%i items loaded.", this->data.size()-1);
}

EIF_Data& EIF::Get(unsigned int id)
{
	if (id < this->data.size())
		return this->data[id];
	else
		return this->data[0];
}

const EIF_Data& EIF::Get(unsigned int id) const
{
	if (id < this->data.size())
		return this->data[id];
	else
		return this->data[0];
}

unsigned int EIF::GetKey(int keynum) const
{
	for (std::size_t i = 0; i < this->data.size(); ++i)
	{
		if (this->Get(i).type == EIF::Key && this->Get(i).key == keynum)
			return i;
	}

	return 0;
}

void ENF::Read(const std::string& filename)
{
	const auto file = ReadPub<pub::Enf>(filename);
	this->rid = file.rid;
	this->length = file.total_npcs_count;

	LoadRecords(filename, this->length, file.npcs, this->data, [](const pub::EnfRecord& record, ENF_Data& newdata)
	{
		newdata.graphic = record.graphic_id;

		newdata.boss = record.boss;
		newdata.child = record.child;
		newdata.type = static_cast<ENF::Type>(record.type);
		newdata.vendor_id = record.behavior_id;
		newdata.hp = record.hp;

		newdata.mindam = record.min_damage;
		newdata.maxdam = record.max_damage;

		newdata.accuracy = record.accuracy;
		newdata.evade = record.evade;
		newdata.armor = record.armor;

		newdata.exp = record.experience;
	});

	Console::Out("%i npc types loaded.", this->data.size()-1);
}

ENF_Data& ENF::Get(unsigned int id)
{
	if (id < this->data.size())
		return this->data[id];
	else
		return this->data[0];
}

const ENF_Data& ENF::Get(unsigned int id) const
{
	if (id < this->data.size())
		return this->data[id];
	else
		return this->data[0];
}

void ESF::Read(const std::string& filename)
{
	const auto file = ReadPub<pub::Esf>(filename);
	this->rid = file.rid;
	this->length = file.total_skills_count;

	LoadRecords(filename, this->length, file.skills, this->data, [](const pub::EsfRecord& record, ESF_Data& newdata)
	{
		newdata.shout = record.chant;

		newdata.icon = record.icon_id;
		newdata.graphic = record.graphic_id;
		newdata.tp = record.tp_cost;
		newdata.sp = record.sp_cost;
		newdata.cast_time = record.cast_time;

		newdata.type = static_cast<ESF::Type>(record.type);
		newdata.target_restrict = static_cast<ESF::TargetRestrict>(record.target_restrict);
		newdata.target = static_cast<ESF::Target>(record.target_type);

		newdata.mindam = record.min_damage;
		newdata.maxdam = record.max_damage;
		newdata.accuracy = record.accuracy;
		newdata.hp = record.hp_heal;
	});

	Console::Out("%i spells loaded.", this->data.size()-1);
}

ESF_Data& ESF::Get(unsigned int id)
{
	if (id < this->data.size())
		return this->data[id];
	else
		return this->data[0];
}

const ESF_Data& ESF::Get(unsigned int id) const
{
	if (id < this->data.size())
		return this->data[id];
	else
		return this->data[0];
}

void ECF::Read(const std::string& filename)
{
	const auto file = ReadPub<pub::Ecf>(filename);
	this->rid = file.rid;
	this->length = file.total_classes_count;

	LoadRecords(filename, this->length, file.classes, this->data, [](const pub::EcfRecord& record, ECF_Data& newdata)
	{
		newdata.base = record.parent_type;
		newdata.type = record.stat_group;

		newdata.str = record.str;
		newdata.intl = record.intl;
		newdata.wis = record.wis;
		newdata.agi = record.agi;
		newdata.con = record.con;
		newdata.cha = record.cha;
	});

	Console::Out("%i classes loaded.", this->data.size()-1);
}

ECF_Data& ECF::Get(unsigned int id)
{
	if (id < this->data.size())
		return this->data[id];
	else
		return this->data[0];
}

const ECF_Data& ECF::Get(unsigned int id) const
{
	if (id < this->data.size())
		return this->data[id];
	else
		return this->data[0];
}
