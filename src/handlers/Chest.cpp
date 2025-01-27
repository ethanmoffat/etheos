
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "handlers.hpp"

#include "../character.hpp"
#include "../config.hpp"
#include "../eodata.hpp"
#include "../map.hpp"
#include "../packet.hpp"
#include "../world.hpp"

#include "../util.hpp"

#include <algorithm>

namespace Handlers
{

void Chest_Add(Character *character, PacketReader &reader)
{
	if (character->trading) return;
	if (!character->CanInteractItems()) return;

	int x = reader.GetChar();
	int y = reader.GetChar();
	int id = reader.GetShort();
	int amount = reader.GetThree();

	if (character->world->eif->Get(id).special == EIF::Lore)
	{
		return;
	}

	if (util::path_length(character->x, character->y, x, y) <= 1)
	{
		if (character->map->GetSpec(x, y) == Map_Tile::Chest)
		{
			UTIL_FOREACH(character->map->chests, chest)
			{
				if (chest->x == x && chest->y == y)
				{
					amount = std::min(amount, int(character->world->config["MaxChest"]) - chest->HasItem(id));

					if (character->HasItem(id) >= amount && chest->AddItem(id, amount))
					{
						character->DelItem(id, amount);
						chest->Update(character->map, character);

						PacketBuilder reply(PACKET_CHEST, PACKET_REPLY, 8 + chest->items.size() * 5);
						reply.AddShort(id);
						reply.AddInt(character->HasItem(id));
						reply.AddChar(static_cast<unsigned char>(character->weight));
						reply.AddChar(static_cast<unsigned char>(character->maxweight));

						UTIL_CIFOREACH(chest->items, item)
						{
							if (item->id != 0)
							{
								reply.AddShort(item->id);
								reply.AddThree(item->amount);
							}
						}

						character->Send(reply);
					}

					break;
				}
			}
		}
	}
}

// Taking an item from a chest
void Chest_Take(Character *character, PacketReader &reader)
{
	int x = reader.GetChar();
	int y = reader.GetChar();
	int id = reader.GetShort();

	if (util::path_length(character->x, character->y, x, y) <= 1)
	{
		if (character->map->GetSpec(x, y) == Map_Tile::Chest)
		{
			UTIL_FOREACH(character->map->chests, chest)
			{
				if (chest->x == x && chest->y == y)
				{
					int amount = chest->HasItem(id);
					int taken = character->CanHoldItem(id, amount);

					if (taken > 0)
					{
						chest->DelSomeItem(id, taken);
						character->AddItem(id, taken);

						PacketBuilder reply(PACKET_CHEST, PACKET_GET, 7 + (chest->items.size() + 1) * 5);
						reply.AddShort(id);
						reply.AddThree(taken);
						reply.AddChar(static_cast<unsigned char>(character->weight));
						reply.AddChar(static_cast<unsigned char>(character->maxweight));

						UTIL_CIFOREACH(chest->items, item)
						{
							if (item->id != 0)
							{
								reply.AddShort(item->id);
								reply.AddThree(item->amount);
							}
						}

						character->Send(reply);

						chest->Update(character->map, character);
						break;
					}
				}
			}
		}
	}
}

// Opening a chest
void Chest_Open(Character *character, PacketReader &reader)
{
	int x = reader.GetChar();
	int y = reader.GetChar();

	if (util::path_length(character->x, character->y, x, y) <= 1)
	{
		if (character->map->GetSpec(x, y) == Map_Tile::Chest)
		{
			UTIL_FOREACH(character->map->chests, chest)
			{
				if (chest->x == x && chest->y == y)
				{
					PacketBuilder reply(PACKET_CHEST, PACKET_OPEN, 2);

					if (chest->key > 0)
					{
						// Thanks daddy Sausage
						// https://discord.com/channels/723989119503696013/787685796055482368/1046490342834315325

						unsigned int key_item = character->world->eif->GetKey(chest->key);
						if (!key_item)
						{
							reply.SetID(PACKET_CHEST, PACKET_CLOSE);
							reply.AddString("N");
							character->Send(reply);
							break;
						}
						else if (!character->HasItem(key_item))
						{
							reply.SetID(PACKET_CHEST, PACKET_CLOSE);
							reply.AddShort(chest->key);
							character->Send(reply);
							break;
						}
					}

					reply.AddChar(x);
					reply.AddChar(y);

					reply.ReserveMore(chest->items.size() * 5);

					UTIL_CIFOREACH(chest->items, item)
					{
						if (item->id != 0)
						{
							reply.AddShort(item->id);
							reply.AddThree(item->amount);
						}
					}

					character->Send(reply);
					break;
				}
			}
		}
	}
}

PACKET_HANDLER_REGISTER(PACKET_CHEST)
	Register(PACKET_ADD, Chest_Add, Playing);
	Register(PACKET_TAKE, Chest_Take, Playing);
	Register(PACKET_OPEN, Chest_Open, Playing);
PACKET_HANDLER_REGISTER_END(PACKET_CHEST)

}
