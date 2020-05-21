
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
std::string sha256(const std::string&);

/**
 * Convert a string to the hex representation of its bcrypt hash
 */
std::string bcrypt(const std::string&);

#endif // HASH_HPP_INCLUDED
