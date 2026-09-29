
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef PACKET_HPP_INCLUDED
#define PACKET_HPP_INCLUDED

#include "fwd/packet.hpp"

#include <array>
#include <cstddef>
#include <string>

/**
 * Legacy helpers for EO numbers and packet IDs.
 */
class PacketProcessor
{
	public:
		/**
		 * Highest number EO can represent with 1 byte.
		 */
		static const unsigned int MAX1 = 253;

		/**
		 * Highest number EO can represent with 2 bytes.
		 */
		static const unsigned int MAX2 = 64009;

		/**
		 * Highest number EO can represent with 3 bytes.
		 */
		static const unsigned int MAX3 = 16194277;

		/**
		 * Return a string describing a packet's family ID.
		 */
		static std::string GetFamilyName(PacketFamily family);

		/**
		 * Return a string describing a packet's action ID.
		 */
		static std::string GetActionName(PacketAction action);

		static unsigned int Number(unsigned char, unsigned char = 254, unsigned char = 254, unsigned char = 254);
		static std::array<unsigned char, 4> ENumber(unsigned int);
		static std::array<unsigned char, 4> ENumber(unsigned int, std::size_t &size);

		static unsigned short PID(PacketFamily family, PacketAction action);
		static std::array<unsigned char, 2> EPID(unsigned short id);
};

class PacketReader
{
	protected:
		std::string data;
		std::size_t pos;

	public:
		PacketReader(const std::string &);

		std::size_t Length() const;
		std::size_t Remaining() const;

		PacketAction Action() const;
		PacketFamily Family() const;

		unsigned int GetNumber(std::size_t length);

		unsigned char GetByte();
		unsigned char GetChar();
		unsigned short GetShort();
		unsigned int GetThree();
		unsigned int GetInt();

		std::string GetFixedString(std::size_t length);
		std::string GetBreakString(unsigned char breakchar = 0xFF);
		std::string GetEndString();

		~PacketReader();
};

class PacketBuilder
{
	protected:
		unsigned short id;
		std::string data;
		std::size_t add_size;

	public:
		PacketBuilder(PacketFamily family = PACKET_F_INIT, PacketAction action = PACKET_A_INIT, std::size_t size_guess = 0);

		unsigned short SetID(unsigned short id);
		unsigned short SetID(PacketFamily family, PacketAction action);

		unsigned short GetID() const;

		std::size_t Length() const;
		std::size_t Capacity() const;

		void ReserveMore(std::size_t size_guess);

		PacketBuilder &AddByte(unsigned char);
		PacketBuilder &AddChar(unsigned char);
		PacketBuilder &AddShort(unsigned short);
		PacketBuilder &AddThree(unsigned int);
		PacketBuilder &AddInt(unsigned int);
		PacketBuilder &AddVar(int min, int max, unsigned int);

		PacketBuilder &AddString(const std::string &);
		PacketBuilder &AddBreakString(const std::string &, unsigned char breakchar = 0xFF);

		void AddSize(std::size_t size);

		void Reset(std::size_t size_guess = 0);

		std::string Get() const;

		operator std::string() const;
		bool operator==(const PacketBuilder& rhs) const;

		~PacketBuilder();
};

#endif // PACKET_HPP_INCLUDED
