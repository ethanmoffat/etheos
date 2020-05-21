
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "hash.hpp"

#include "sha256.h"
#include "bcrypt/BCrypt.hpp"

#include <string>

std::string sha256(const std::string& str, const std::string& salt)
{
	sha256_context ctx;
	char digest[32];
	char cdigest[64];

	std::string toHash = salt + str;

	sha256_start(&ctx);
	sha256_update(&ctx, toHash.c_str(), toHash.length());
	sha256_finish(&ctx, digest);

	for (int i = 0; i < 32; ++i)
	{
		cdigest[i*2]   = "0123456789abcdef"[((digest[i] >> 4) & 0x0F)];
		cdigest[i*2+1] = "0123456789abcdef"[((digest[i]) & 0x0F)];
	}

	return std::string(cdigest, 64);
}

int bcrypt_generatesalt(char * salt, int workfactor)
{
	return bcrypt_gensalt(workfactor, salt);
}

std::string bcrypt(const std::string& str, const std::string& salt)
{
	static int workFactor = 12;

	char retHash[BCRYPT_HASHSIZE] = {0};
	int hashRes = bcrypt_hashpw(str.c_str(), salt.c_str(), retHash);
	if (hashRes != 0)
	{
		throw std::runtime_error("Unable to compute bcrypt hash");
	}

	return std::string(retHash, BCRYPT_HASHSIZE);
}