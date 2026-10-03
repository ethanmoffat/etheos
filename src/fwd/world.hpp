
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef FWD_WORLD_HPP_INCLUDED
#define FWD_WORLD_HPP_INCLUDED

class World;

struct Board;
struct Board_Post;
struct Home;

enum WarpAnimation : unsigned char
{
	WARP_ANIMATION_NONE = 0,
	WARP_ANIMATION_SCROLL = 1,
	WARP_ANIMATION_ADMIN = 2,
	WARP_ANIMATION_INVALID = 255,
};

enum WarpReply : unsigned char
{
	WARP_LOCAL = 1,
	WARP_SWITCH = 2,
};

#endif // FWD_WORLD_HPP_INCLUDED
