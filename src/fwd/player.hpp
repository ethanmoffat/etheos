
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef FWD_PLAYER_HPP_INCLUDED
#define FWD_PLAYER_HPP_INCLUDED

struct AccountCreateInfo;
struct PasswordChangeInfo;
struct AccountCredentials;

class Player;

enum CharacterReply : short
{
	CHARACTER_EXISTS = 1,
	CHARACTER_FULL = 2,
	CHARACTER_NOT_APPROVED = 4,
	CHARACTER_OK = 5,
	CHARACTER_DELETED = 6,
};

enum WelcomeReply : short
{
	WELCOME_GRANTED = 1,
	WELCOME_COMPLETED = 2,
	WELCOME_ERROR = 3
};

#endif // FWD_PLAYER_HPP_INCLUDED
