
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef FWD_WEDDING_HPP_INCLUDED
#define FWD_WEDDING_HPP_INCLUDED

class Wedding;

enum MarriageReply : short
{
	MARRIAGE_ALREADY_HAVE_PARTNER = 1,
	MARRIAGE_DIVORCE_NOT_MARRIED = 2,
	MARRIAGE_SUCCESS = 3,
	MARRIAGE_NOT_ENOUGH_GP = 4,
	MARRIAGE_DIVORCE_WRONG_NAME = 5,
	MARRIAGE_DIVORCE_SERVICE_BUSY = 6,
	MARRIAGE_DIVORCE_NOTIFICATION = 7
};

enum PriestReply : short
{
	PRIEST_NOT_DRESSED = 1,
	PRIEST_LOW_LEVEL = 2,
	PRIEST_PARTNER_NOT_PRESENT = 3,
	PRIEST_PARTNER_NOT_DRESSED = 4,
	PRIEST_BUSY = 5,
	PRIEST_DO_YOU = 6,
	PRIEST_PARTNER_ALREADY_MARRIED = 7,
	PRIEST_NO_PERMISSION = 8
};

#endif // FWD_WEDDING_HPP_INCLUDED
