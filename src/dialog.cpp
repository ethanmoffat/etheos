
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "dialog.hpp"

#include <string>
#include <utility>

Dialog::Dialog()
{

}

void Dialog::AddPage(const std::string& text)
{
	this->pages.push_back(text);
}

void Dialog::AddLink(int id, const std::string& text)
{
	this->links.insert(std::make_pair(id, text));
}

bool Dialog::CheckLink(int id) const
{
	return this->links.find(id) != this->links.end();
}

const std::deque<std::string>& Dialog::Pages() const
{
	return this->pages;
}

const std::multimap<int, std::string>& Dialog::Links() const
{
	return this->links;
}

Dialog::~Dialog()
{

}
