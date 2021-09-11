
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef EOSERV_WINDOWS_H_INCLUDED
#define EOSERV_WINDOWS_H_INCLUDED

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN

// Include winsock2.h before windows.h to prevent winsock.h being included
#include <winsock2.h>
#include <windows.h>

// Non-lean-and-mean headers we need
#include <mmsystem.h>

// Undefine conflicting function names:

// Character::PlaySound
#ifdef PlaySound
#undef PlaySound
#endif // PlaySound

// std::min conflicts with min in winmindef.h
#ifdef min
#undef min
#endif

// std::max conflicts with max in winmindef.h
#ifdef max
#undef max
#endif

#define setenv(_Name, _Value, _Overwrite) _putenv_s(_Name, _Value)

#endif // EOSERV_WINDOWS_H_INCLUDED
