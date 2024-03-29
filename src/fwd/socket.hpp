
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef FWD_SOCKET_HPP_INCLUDED
#define FWD_SOCKET_HPP_INCLUDED

class IPAddress;
class Client;
class Server;

enum LogConnection : unsigned char
{
    LogAll = 0,
    FilterPrivate,
    FilterAll
};

/**
 * Return the OS last error message
 */
const char *OSErrorString();

#endif // FWD_SOCKET_HPP_INCLUDED
