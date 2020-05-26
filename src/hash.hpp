
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef HASH_HPP_INCLUDED
#define HASH_HPP_INCLUDED

#include <string>
#include "util/secure_string.hpp"

enum HashFunc
{
    NONE = 0,
    SHA256 = 1,
    BCRYPT = 2
};

class Hasher
{
public:
    const HashFunc hashFunc;

    Hasher(HashFunc hashFunc) : hashFunc(hashFunc) { }

    virtual std::string hash(const std::string& input) const = 0;
    virtual bool check(const std::string& toCheck, const std::string& hashed) const = 0;

    static util::secure_string SaltPassword(const std::string& salt, const std::string& username, util::secure_string&& password)
    {
        return util::secure_string(salt + username + std::move(password.str()));
    }
};

/**
 * Convert a string to the hex representation of its sha256 hash
 */
class Sha256Hasher : public Hasher {
public:
    Sha256Hasher() : Hasher(SHA256) { }

    virtual std::string hash(const std::string& input) const override;
    virtual bool check(const std::string& toCheck, const std::string& hashed) const override;
};

/**
 * Convert a string to a bcrypt hash
 */
class BcryptHasher : public Hasher {
private:
    int _workload;

public:
    BcryptHasher(int workload) : Hasher(BCRYPT), _workload(workload) { }

    virtual std::string hash(const std::string& input) const override;
    virtual bool check(const std::string& toCheck, const std::string& hashed) const override;
};

#endif // HASH_HPP_INCLUDED
