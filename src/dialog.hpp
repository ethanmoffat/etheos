
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef DIALOG_HPP_INCLUDED
#define DIALOG_HPP_INCLUDED

#include "fwd/dialog.hpp"

#include <deque>
#include <map>
#include <string>

class Dialog
{
	private:
		std::deque<std::string> pages;
		std::multimap<int, std::string> links;

	public:
		Dialog();

		void AddPage(const std::string&);
		void AddLink(int, const std::string&);
		bool CheckLink(int) const;

		const std::deque<std::string>& Pages() const;
		const std::multimap<int, std::string>& Links() const;

		~Dialog();
};

#endif // DIALOG_HPP_INCLUDED
