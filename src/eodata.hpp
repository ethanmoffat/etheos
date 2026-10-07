
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef EODATA_HPP_INCLUDED
#define EODATA_HPP_INCLUDED

#include "fwd/eodata.hpp"

#include <eolib/protocol/enums.hpp>
#include <eolib/protocol/pub/enums.hpp>
#include <eolib/protocol/pub/structs.hpp>

#include <string>
#include <vector>

// etheos treats this reserved item subtype as two-handed: a weapon with it can't be used with a shield
constexpr eolib::protocol::pub::ItemSubtype ItemSubtypeTwoHanded = eolib::protocol::pub::ItemSubtype::Reserved4;

// spec1-3 mean different things per item type. The As* views don't check the type.

struct ItemEquipmentSpec
{
	int doll_graphic;
	// Armor only
	eolib::protocol::Gender gender;
};

inline ItemEquipmentSpec AsEquipment(const eolib::protocol::pub::EifRecord& item)
{
	return ItemEquipmentSpec{item.spec1, static_cast<eolib::protocol::Gender>(item.spec2)};
}

// Two-handed weapon (ItemSubtypeTwoHanded). A non-zero graphic is shown in the shield slot and blocks every shield,
// including arrows and wings.
struct ItemTwoHandedSpec
{
	int dual_wield_doll_graphic;
};

inline ItemTwoHandedSpec AsTwoHanded(const eolib::protocol::pub::EifRecord& item)
{
	return ItemTwoHandedSpec{item.spec3};
}

// Teleport. map 0 warps the character home.
struct ItemScrollSpec
{
	int map;
	int x;
	int y;
};

inline ItemScrollSpec AsScroll(const eolib::protocol::pub::EifRecord& item)
{
	return ItemScrollSpec{item.spec1, item.spec2, item.spec3};
}

struct ItemExpRewardSpec
{
	int exp;
};

inline ItemExpRewardSpec AsExpReward(const eolib::protocol::pub::EifRecord& item)
{
	return ItemExpRewardSpec{item.spec1};
}

struct ItemHairDyeSpec
{
	int hair_color;
};

inline ItemHairDyeSpec AsHairDye(const eolib::protocol::pub::EifRecord& item)
{
	return ItemHairDyeSpec{item.spec1};
}

struct ItemEffectPotionSpec
{
	int effect;
};

inline ItemEffectPotionSpec AsEffectPotion(const eolib::protocol::pub::EifRecord& item)
{
	return ItemEffectPotionSpec{item.spec1};
}

struct ItemKeySpec
{
	int key;
};

inline ItemKeySpec AsKey(const eolib::protocol::pub::EifRecord& item)
{
	return ItemKeySpec{item.spec1};
}

struct ItemAlcoholSpec
{
	int potency;
};

inline ItemAlcoholSpec AsAlcohol(const eolib::protocol::pub::EifRecord& item)
{
	return ItemAlcoholSpec{item.spec1};
}

class EIF
{
	public:
		explicit EIF(const std::string& filename) { Read(filename); }

		void Read(const std::string& filename);

		/**
		 * Gets the item with the given id. Ids start at 1. Returns an empty item if there's none.
		 */
		const eolib::protocol::pub::EifRecord& Get(int id) const;

		bool Exists(int id) const;

		/**
		 * Sets record to nullptr and returns false if there's no record with the given id.
		 */
		bool TryGet(int id, const eolib::protocol::pub::EifRecord*& record) const;

		int Count() const;

		const std::vector<int>& Rid() const { return this->file.rid; }

		/**
		 * Gets the record count from the file header.
		 */
		int Length() const { return this->file.total_items_count; }

		/**
		 * Gets the id of the key item that opens the given lock, or 0 if there's none.
		 */
		int GetKey(int keynum) const;

	private:
		eolib::protocol::pub::Eif file;
};

class ENF
{
	public:
		explicit ENF(const std::string& filename) { Read(filename); }

		void Read(const std::string& filename);

		/**
		 * Gets the NPC with the given id. Ids start at 1. Returns an empty NPC if there's none.
		 */
		const eolib::protocol::pub::EnfRecord& Get(int id) const;

		bool Exists(int id) const;

		/**
		 * Sets record to nullptr and returns false if there's no record with the given id.
		 */
		bool TryGet(int id, const eolib::protocol::pub::EnfRecord*& record) const;

		int Count() const;

		const std::vector<int>& Rid() const { return this->file.rid; }

		/**
		 * Gets the record count from the file header.
		 */
		int Length() const { return this->file.total_npcs_count; }

	private:
		eolib::protocol::pub::Enf file;
};

class ESF
{
	public:
		explicit ESF(const std::string& filename) { Read(filename); }

		void Read(const std::string& filename);

		/**
		 * Gets the spell with the given id. Ids start at 1. Returns an empty spell if there's none.
		 */
		const eolib::protocol::pub::EsfRecord& Get(int id) const;

		bool Exists(int id) const;

		/**
		 * Sets record to nullptr and returns false if there's no record with the given id.
		 */
		bool TryGet(int id, const eolib::protocol::pub::EsfRecord*& record) const;

		int Count() const;

		const std::vector<int>& Rid() const { return this->file.rid; }

		/**
		 * Gets the record count from the file header.
		 */
		int Length() const { return this->file.total_skills_count; }

	private:
		eolib::protocol::pub::Esf file;
};

class ECF
{
	public:
		explicit ECF(const std::string& filename) { Read(filename); }

		void Read(const std::string& filename);

		/**
		 * Gets the class with the given id. Ids start at 1. Returns an empty class if there's none.
		 */
		const eolib::protocol::pub::EcfRecord& Get(int id) const;

		bool Exists(int id) const;

		/**
		 * Sets record to nullptr and returns false if there's no record with the given id.
		 */
		bool TryGet(int id, const eolib::protocol::pub::EcfRecord*& record) const;

		int Count() const;

		/**
		 * Checks if class_id is required_class or one of its subclasses (through the parent_type chain). A
		 * required_class of 0 matches every class.
		 */
		bool ClassMatches(int class_id, int required_class) const;

		const std::vector<int>& Rid() const { return this->file.rid; }

		/**
		 * Gets the record count from the file header.
		 */
		int Length() const { return this->file.total_classes_count; }

	private:
		eolib::protocol::pub::Ecf file;
};

#endif // EODATA_HPP_INCLUDED
