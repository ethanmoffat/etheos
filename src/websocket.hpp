
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef WEBSOCKET_HPP_INCLUDED
#define WEBSOCKET_HPP_INCLUDED

#include <cstdint>
#include <cstring>
#include <string>

/**
 * Utilities for the WebSocket protocol (RFC 6455).
 * Provides SHA-1 (for handshake key), base64 encoding, and frame encode/decode.
 */
namespace websocket
{

namespace detail
{

inline uint32_t rotl32(uint32_t n, unsigned int c)
{
	return (n << c) | (n >> (32 - c));
}

/**
 * Compute SHA-1 digest of input.
 * Returns a 20-byte binary string.
 */
inline std::string sha1(const std::string &input)
{
	uint32_t h0 = 0x67452301u;
	uint32_t h1 = 0xEFCDAB89u;
	uint32_t h2 = 0x98BADCFEu;
	uint32_t h3 = 0x10325476u;
	uint32_t h4 = 0xC3D2E1F0u;

	// Pre-process: pad message
	std::string msg = input;
	uint64_t orig_bits = static_cast<uint64_t>(input.size()) * 8;

	msg += '\x80';
	while (msg.size() % 64 != 56)
		msg += '\x00';

	// Append original bit length as big-endian 64-bit integer
	for (int i = 7; i >= 0; --i)
		msg += static_cast<char>((orig_bits >> (i * 8)) & 0xFF);

	// Process each 512-bit (64-byte) chunk
	for (std::size_t i = 0; i < msg.size(); i += 64)
	{
		uint32_t w[80];

		for (int j = 0; j < 16; ++j)
		{
			w[j] = (static_cast<uint8_t>(msg[i + j*4])     << 24)
			      | (static_cast<uint8_t>(msg[i + j*4 + 1]) << 16)
			      | (static_cast<uint8_t>(msg[i + j*4 + 2]) << 8)
			      |  static_cast<uint8_t>(msg[i + j*4 + 3]);
		}

		for (int j = 16; j < 80; ++j)
			w[j] = rotl32(w[j-3] ^ w[j-8] ^ w[j-14] ^ w[j-16], 1);

		uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;

		for (int j = 0; j < 80; ++j)
		{
			uint32_t f, k;

			if      (j < 20) { f = (b & c) | (~b & d);           k = 0x5A827999u; }
			else if (j < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1u; }
			else if (j < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
			else             { f = b ^ c ^ d;                    k = 0xCA62C1D6u; }

			uint32_t temp = rotl32(a, 5) + f + e + k + w[j];
			e = d; d = c; c = rotl32(b, 30); b = a; a = temp;
		}

		h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
	}

	// Produce 20-byte digest
	std::string digest(20, '\0');
	for (int i = 0; i < 4; ++i) {
		digest[i]      = static_cast<char>((h0 >> (24 - i*8)) & 0xFF);
		digest[4 + i]  = static_cast<char>((h1 >> (24 - i*8)) & 0xFF);
		digest[8 + i]  = static_cast<char>((h2 >> (24 - i*8)) & 0xFF);
		digest[12 + i] = static_cast<char>((h3 >> (24 - i*8)) & 0xFF);
		digest[16 + i] = static_cast<char>((h4 >> (24 - i*8)) & 0xFF);
	}

	return digest;
}

/**
 * Base64-encode binary data.
 */
inline std::string base64_encode(const std::string &input)
{
	static const char *table =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

	std::string out;
	out.reserve(((input.size() + 2) / 3) * 4);

	for (std::size_t i = 0; i < input.size(); i += 3)
	{
		uint32_t b = static_cast<uint8_t>(input[i]) << 16;

		if (i + 1 < input.size()) b |= static_cast<uint8_t>(input[i + 1]) << 8;
		if (i + 2 < input.size()) b |= static_cast<uint8_t>(input[i + 2]);

		out += table[(b >> 18) & 63];
		out += table[(b >> 12) & 63];
		out += (i + 1 < input.size()) ? table[(b >> 6) & 63] : '=';
		out += (i + 2 < input.size()) ? table[ b       & 63] : '=';
	}

	return out;
}

} // namespace detail

/**
 * Build the HTTP 101 upgrade response for a WebSocket handshake.
 * @param key  The value of the client's Sec-WebSocket-Key header.
 */
inline std::string build_handshake_response(const std::string &key)
{
	static const std::string MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	std::string accept = detail::base64_encode(detail::sha1(key + MAGIC));

	return "HTTP/1.1 101 Switching Protocols\r\n"
	       "Upgrade: websocket\r\n"
	       "Connection: Upgrade\r\n"
	       "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
}

/**
 * Build a WebSocket close frame (server->client, unmasked, no payload).
 */
inline std::string wrap_close_frame()
{
	return std::string("\x88\x00", 2); // FIN=1, opcode=8 (close), no mask, 0 payload
}

/**
 * Wrap payload in a binary WebSocket frame (server->client, unmasked).
 * Opcode 0x2 (binary), FIN=1.
 */
inline std::string wrap_frame(const std::string &payload)
{
	std::string frame;
	std::size_t len = payload.size();

	frame += '\x82'; // FIN=1, RSV=0, opcode=2 (binary)

	if (len <= 125)
	{
		frame += static_cast<char>(len);
	}
	else if (len <= 65535)
	{
		frame += '\x7E'; // extended 16-bit length
		frame += static_cast<char>((len >> 8) & 0xFF);
		frame += static_cast<char>( len       & 0xFF);
	}
	else
	{
		frame += '\x7F'; // extended 64-bit length
		for (int i = 7; i >= 0; --i)
			frame += static_cast<char>((len >> (i * 8)) & 0xFF);
	}

	frame += payload;
	return frame;
}

// Opcodes
static const uint8_t WS_OPCODE_CONTINUATION = 0x0;
static const uint8_t WS_OPCODE_TEXT         = 0x1;
static const uint8_t WS_OPCODE_BINARY       = 0x2;
static const uint8_t WS_OPCODE_CLOSE        = 0x8;
static const uint8_t WS_OPCODE_PING         = 0x9;
static const uint8_t WS_OPCODE_PONG         = 0xA;

/**
 * Attempt to decode one WebSocket frame from buf[0..len-1].
 * @param buf          Raw bytes (client-to-server, expected to be masked).
 * @param len          Number of bytes available.
 * @param out_payload  Decoded (unmasked) payload on success.
 * @param out_opcode   Frame opcode.
 * @param out_consumed Number of bytes consumed from buf on success.
 * @return true if a complete frame was decoded, false if more data is needed.
 */
inline bool decode_frame(const char *buf, std::size_t len,
                         std::string &out_payload, uint8_t &out_opcode,
                         std::size_t &out_consumed)
{
	if (len < 2)
		return false;

	uint8_t b0 = static_cast<uint8_t>(buf[0]);
	uint8_t b1 = static_cast<uint8_t>(buf[1]);

	out_opcode = b0 & 0x0F;
	bool masked = (b1 & 0x80) != 0;
	uint64_t payload_len = b1 & 0x7F;

	std::size_t header_len = 2;

	if (payload_len == 126)
	{
		if (len < 4) return false;
		payload_len = (static_cast<uint64_t>(static_cast<uint8_t>(buf[2])) << 8)
		             | static_cast<uint8_t>(buf[3]);
		header_len = 4;
	}
	else if (payload_len == 127)
	{
		if (len < 10) return false;
		payload_len = 0;
		for (int i = 0; i < 8; ++i)
			payload_len = (payload_len << 8) | static_cast<uint8_t>(buf[2 + i]);
		header_len = 10;
	}

	if (masked)
		header_len += 4;

	if (len < header_len + static_cast<std::size_t>(payload_len))
		return false;

	const char *mask_key = nullptr;
	if (masked)
		mask_key = buf + header_len - 4;

	out_payload.resize(static_cast<std::size_t>(payload_len));
	for (std::size_t i = 0; i < static_cast<std::size_t>(payload_len); ++i)
	{
		uint8_t byte = static_cast<uint8_t>(buf[header_len + i]);
		if (masked)
			byte ^= static_cast<uint8_t>(mask_key[i % 4]);
		out_payload[i] = static_cast<char>(byte);
	}

	out_consumed = header_len + static_cast<std::size_t>(payload_len);
	return true;
}

} // namespace websocket

#endif // WEBSOCKET_HPP_INCLUDED
