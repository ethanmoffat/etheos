
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef HASH_HPP_INCLUDED
#define HASH_HPP_INCLUDED

#include <string>

enum HashFunc
{
    SHA256 = 1,
    BCRYPT = 2
};

/**
 * Convert a string to the hex representation of its sha256 hash
 */
std::string sha256(const std::string& input, const std::string& salt);

/**
 * Generate a salt compatible with the bcrypt APIs
 */
int bcrypt_generatesalt(char * salt, int workfactor = 12);

/**
 * Convert a string to a bcrypt hash
 */
std::string bcrypt(const std::string& input, const std::string& salt);

#endif // HASH_HPP_INCLUDED
