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

// Warns on a header count mismatch and drops the trailing "eof" marker record some editors write
template <typename TRecord>
static void CheckRecords(const std::string& filename, int length, std::vector<TRecord>& records)
{
	if (static_cast<int>(records.size()) != length)
	{
		Console::Wrn("File may be invalid: %s (has %zu records, header says %i)", filename.c_str(), records.size(),
			length);
	}

	if (!records.empty() && records.back().name == "eof")
	{
		records.pop_back();
	}
}

template <typename TRecord>
static bool RecordExists(const std::vector<TRecord>& records, int id)
{
	return id >= 1 && id <= static_cast<int>(records.size());
}

template <typename TRecord>
static const TRecord& GetRecord(const std::vector<TRecord>& records, int id, const TRecord& empty)
{
	return RecordExists(records, id) ? records[id - 1] : empty;
}

template <typename TRecord>
static bool TryGetRecord(const std::vector<TRecord>& records, int id, const TRecord*& record)
{
	record = RecordExists(records, id) ? &records[id - 1] : nullptr;
	return record != nullptr;
}

static pub::EifRecord MakeEmptyItem()
{
	pub::EifRecord item;
	item.special = pub::ItemSpecial::Lore;
	return item;
}

static pub::EsfRecord MakeEmptySpell()
{
	pub::EsfRecord spell;
	spell.type = pub::SkillType::Attack;
	return spell;
}

// Classless characters (class 0) use the Peasant stat group's formulas
static pub::EcfRecord MakeEmptyClass()
{
	pub::EcfRecord record;
	record.parent_type = 4;
	record.stat_group = 4;
	return record;
}

static const pub::EifRecord empty_item = MakeEmptyItem();
static const pub::EnfRecord empty_npc;
static const pub::EsfRecord empty_spell = MakeEmptySpell();
static const pub::EcfRecord empty_class = MakeEmptyClass();

void EIF::Read(const std::string& filename)
{
	this->file = ReadPub<pub::Eif>(filename);
	CheckRecords(filename, this->file.total_items_count, this->file.items);

	// Ranged gun hack
	constexpr int gun_id = 365;

	if (RecordExists(this->file.items, gun_id) && this->file.items[gun_id - 1].name == "Gun")
	{
		this->file.items[gun_id - 1].subtype = pub::ItemSubtype::Ranged;
	}

	Console::Out("%i items loaded.", this->Count());
}

const pub::EifRecord& EIF::Get(int id) const
{
	return GetRecord(this->file.items, id, empty_item);
}

bool EIF::Exists(int id) const
{
	return RecordExists(this->file.items, id);
}

bool EIF::TryGet(int id, const pub::EifRecord*& record) const
{
	return TryGetRecord(this->file.items, id, record);
}

int EIF::Count() const
{
	return static_cast<int>(this->file.items.size());
}

int EIF::GetKey(int keynum) const
{
	for (int id = 1; id <= this->Count(); ++id)
	{
		const pub::EifRecord& item = this->Get(id);

		if (item.type == pub::ItemType::Key && AsKey(item).key == keynum)
			return id;
	}

	return 0;
}

void ENF::Read(const std::string& filename)
{
	this->file = ReadPub<pub::Enf>(filename);
	CheckRecords(filename, this->file.total_npcs_count, this->file.npcs);

	Console::Out("%i npc types loaded.", this->Count());
}

const pub::EnfRecord& ENF::Get(int id) const
{
	return GetRecord(this->file.npcs, id, empty_npc);
}

bool ENF::Exists(int id) const
{
	return RecordExists(this->file.npcs, id);
}

bool ENF::TryGet(int id, const pub::EnfRecord*& record) const
{
	return TryGetRecord(this->file.npcs, id, record);
}

int ENF::Count() const
{
	return static_cast<int>(this->file.npcs.size());
}

void ESF::Read(const std::string& filename)
{
	this->file = ReadPub<pub::Esf>(filename);
	CheckRecords(filename, this->file.total_skills_count, this->file.skills);

	Console::Out("%i spells loaded.", this->Count());
}

const pub::EsfRecord& ESF::Get(int id) const
{
	return GetRecord(this->file.skills, id, empty_spell);
}

bool ESF::Exists(int id) const
{
	return RecordExists(this->file.skills, id);
}

bool ESF::TryGet(int id, const pub::EsfRecord*& record) const
{
	return TryGetRecord(this->file.skills, id, record);
}

int ESF::Count() const
{
	return static_cast<int>(this->file.skills.size());
}

void ECF::Read(const std::string& filename)
{
	this->file = ReadPub<pub::Ecf>(filename);
	CheckRecords(filename, this->file.total_classes_count, this->file.classes);

	Console::Out("%i classes loaded.", this->Count());
}

const pub::EcfRecord& ECF::Get(int id) const
{
	return GetRecord(this->file.classes, id, empty_class);
}

bool ECF::Exists(int id) const
{
	return RecordExists(this->file.classes, id);
}

bool ECF::TryGet(int id, const pub::EcfRecord*& record) const
{
	return TryGetRecord(this->file.classes, id, record);
}

int ECF::Count() const
{
	return static_cast<int>(this->file.classes.size());
}
