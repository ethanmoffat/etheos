
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef UTIL_SECURE_STRING_HPP_INCLUDED
#define UTIL_SECURE_STRING_HPP_INCLUDED

#include "../util.hpp"

#include <algorithm>
#include <memory>
#include <string>

namespace util
{

/**
 * A string wrapper which automatically nulls out it's memory buffer when
 * destroyed or overwritten
 */
struct secure_string
{
	private:
		std::string str_;

	public:
		secure_string(std::string&& s)
			: str_(s)
		{
			std::fill(UTIL_RANGE(s), '\0');
			s.erase();
		}

		secure_string(const secure_string& other)
		{
			erase();
			this->str_ = other.str_;
		}

		secure_string& operator =(const secure_string& rhs)
		{
			erase();
			this->str_ = rhs.str_;
			return *this;
		}

		secure_string& operator =(secure_string&& rhs)
		{
			erase();
			this->str_ = rhs.str_;
			rhs.erase();
			return *this;
		}

		secure_string& operator =(std::string&& rhs)
		{
			erase();
			this->str_ = rhs;
			std::fill(UTIL_RANGE(rhs), '\0');
			rhs.erase();
			return *this;
		}

		void erase()
		{
			std::fill(UTIL_RANGE(this->str_), '\0');
			this->str_.erase();
		}

		/// Make sure nothing copies or mutates this string!
		const std::string& str()
		{
			return this->str_;
		}

		~secure_string()
		{
			erase();
		}
};

/**
 * Zeros the buffers of the given string members of an object and clears them
 */
template <typename T, typename... Members>
void wipe_members(T& object, Members... members)
{
	auto wipe = [](std::string& s)
	{
		std::fill(UTIL_RANGE(s), '\0');
		s.erase();
	};

	(wipe(object.*members), ...);
}

/**
 * Creates a shared copy of an object that zeros the given string members (such as passwords) when it is destroyed,
 * like secure_string does
 */
template <typename T, typename... Members>
std::shared_ptr<T> make_secure_shared(const T& value, Members... members)
{
	return std::shared_ptr<T>(new T(value), [members...](T* object)
	{
		wipe_members(*object, members...);
		delete object;
	});
}

}

#endif // UTIL_SECURE_STRING_HPP_INCLUDED
