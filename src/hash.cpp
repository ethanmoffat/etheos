
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "hash.hpp"

#include "sha256.h"
#include "bcrypt/BCrypt.hpp"

#include <string>

std::string Sha256Hasher::hash(const std::string& input) const
{
	sha256_context ctx;
	char digest[32];
	char cdigest[64];

	sha256_start(&ctx);
	sha256_update(&ctx, input.c_str(), input.length());
	sha256_finish(&ctx, digest);

	for (int i = 0; i < 32; ++i)
	{
		cdigest[i*2]   = "0123456789abcdef"[((digest[i] >> 4) & 0x0F)];
		cdigest[i*2+1] = "0123456789abcdef"[((digest[i]) & 0x0F)];
	}

	return std::string(cdigest, 64);
}

bool Sha256Hasher::check(const std::string& toCheck, const std::string& hashed) const
{
	return this->hash(toCheck) == hashed;
}

std::string BcryptHasher::hash(const std::string& input) const
{
	return BCrypt::generateHash(input, _workload);
}

bool BcryptHasher::check(const std::string& toCheck, const std::string& hashed) const
{
	return BCrypt::validatePassword(toCheck, hashed);
}