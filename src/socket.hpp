
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef SOCKET_HPP_INCLUDED
#define SOCKET_HPP_INCLUDED

#include "fwd/socket.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "platform.h"

/**
 * Generic Socket exception type
 */
class Socket_Exception : public std::exception
{
	protected:
		const char *err;
	public:
		Socket_Exception(const char *e) : err(e) {};
		const char *error()  const noexcept { return err; };
		virtual const char *what() const noexcept { return "Socket_Exception"; }
};

/**
 * Exception thrown when intializing the socket library failed
 */
class Socket_InitFailed : public Socket_Exception
{
	public:
		Socket_InitFailed(const char *e) : Socket_Exception(e) {}
		const char *what() const noexcept { return "Socket_InitFailed"; }
};

/**
 * Exception thrown when a call to bind() failed
 */
class Socket_BindFailed : public Socket_Exception
{
	public:
		Socket_BindFailed(const char *e) : Socket_Exception(e) {}
		const char *what() const noexcept { return "Socket_BindFailed"; }
};

/**
 * Exception thrown when a call to listen() failed
 */
class Socket_ListenFailed : public Socket_Exception
{
	public:
		Socket_ListenFailed(const char *e) : Socket_Exception(e) {}
		const char *what() const noexcept { return "Socket_ListenFailed"; }
};

/**
 * Exception thrown when a call to select() failed
 */
class Socket_SelectFailed : public Socket_Exception
{
	public:
		Socket_SelectFailed(const char *e) : Socket_Exception(e) {}
		const char *what() const noexcept { return "Socket_SelectFailed"; }
};

/**
 * Static initialization helper for socket subsystem
 */
struct Socket_Init
{
	Socket_Init()
	{
		static bool initialized = false;

		if (!initialized)
		{
			initialized = true;
			init();
		}
	}

	void init();
};

/**
 * Stores an IP address and converts between string and numeric formats.
 */
class IPAddress
{
	protected:
		/**
		 * Integer version of the IP address.
		 */
		std::uint32_t address;

	public:
		/**
		 * Initialize the address as 0.0.0.0.
		 */
		IPAddress();

		/**
		 * Initialize the address to the integer value.
		 */
		IPAddress(unsigned int);

		/**
		 * Initialize the address using 4 octets.
		 */
		IPAddress(unsigned char, unsigned char, unsigned char, unsigned char);

		/**
		 * Initialize the address using a string (eg 255.255.255.255).
		 */
		IPAddress(const char *);

		/**
		 * Initialize the address using a string (eg 255.255.255.255).
		 * Only accepts IP addresses, see Lookup to lookup hostnames
		 */
		IPAddress(std::string);

		/**
		 * Lookup a hostname and use that to create an IPAddress class
		 */
		static IPAddress Lookup(std::string host);

		/**
		 * Set the address to an integer value
		 */
		IPAddress &SetInt(unsigned int);

		/**
		 * Set the address using 4 octets.
		 */
		IPAddress &SetOctets(unsigned char, unsigned char, unsigned char, unsigned char);

		/**
		 * Set the address using a string (eg 255.255.255.255).
		 */
		IPAddress &SetString(const char *);

		/**
		 * Set the address using a string (eg 255.255.255.255).
		 */
		IPAddress &SetString(std::string);

		/**
		 * Set the address to an integer value
		 */
		IPAddress &operator =(unsigned int);

		/**
		 * Set the address using a string (eg 255.255.255.255).
		 */
		IPAddress &operator =(const char *);

		/**
		 * Set the address using a string (eg 255.255.255.255).
		 */
		IPAddress &operator =(std::string);

		/**
		 * Return the IP address as an integer.
		 */
		unsigned int GetInt() const;

		/**
		 * Return the IP address as a string (eg 255.255.255.255).
		 */
		std::string GetString() const;

		/**
		 * Check if the IP address is private per RFC 1918
		 */
		bool IsPrivate() const;

		/**
		 * Return the IP address as an integer.
		 */
		operator unsigned int() const;

		/**
		 * Return the IP address as a string (eg 255.255.255.255).
		 */
		operator std::string() const;

		bool operator ==(const IPAddress &) const;
};

namespace std
{
	template <> struct hash<IPAddress>
	{
		std::size_t operator()(const IPAddress &ipaddress) const
		{
			return ipaddress.GetInt();
		}
	};
}

// Temporary
// TODO: Merge Client and Server with Socket

struct Socket;

/**
 * Generic TCP client class.
 */
class Client
{
	private:
		struct impl_;
		std::unique_ptr<impl_> impl;

		std::uint64_t serial;

	protected:
		Server *server;
		bool connected;
		bool accepted = false;
		std::time_t closed_time;
		std::time_t connect_time;

		// Bytes the OS hasn't accepted yet. They're sent by FlushSend once the socket is writable. Nothing is allocated
		// until a send leaves bytes behind, and the memory is freed once everything has been sent, since large
		// leftovers come from occasional file uploads.
		std::vector<std::uint8_t> send_buffer;

		/**
		 * Writes as much of data as the OS accepts without blocking.
		 * @return The number of bytes written, or -1 if the socket failed.
		 */
		long WriteSome(const std::uint8_t* data, std::size_t length);

	public:
		// Most bytes the send buffer may hold: one max-size EO packet (64,010 bytes framed) plus headroom for the packets
		// queued behind it. The client is closed if more would be needed.
		static constexpr std::size_t SendBufferMaxSize = (64 + 8) * 1024;

		// Holds dozens of typical packets, and grows in steps of the same size for larger ones (file uploads, crowded
		// maps)
		static constexpr std::size_t SendBufferInitialSize = 8 * 1024;
		static constexpr std::size_t SendBufferGrowthStep = 8 * 1024;

		Client();
		Client(const IPAddress &addr, std::uint16_t port);
		Client(Server *);
		Client(const Socket &, Server *);

		bool Connect(const IPAddress &addr, std::uint16_t port);
		void Bind(const IPAddress &addr, std::uint16_t port);

		/**
		 * Reads up to length bytes that have arrived and appends them to buffer. Reserve the buffer first to control
		 * its capacity. The client is closed if the connection was closed by the peer or failed.
		 * @return The number of bytes read, which is 0 if none are available.
		 */
		std::size_t Recv(std::vector<std::uint8_t>& buffer, std::size_t length);

		/**
		 * Sends data, keeping any bytes the OS doesn't accept yet in the send buffer. Data sent after Close(true) is
		 * dropped.
		 */
		void Send(const std::vector<std::uint8_t>& data);

		bool FlushSend();

		bool HasPendingSend() const;

		/**
		 * Waits for the socket, sending waiting bytes once it's writable.
		 * @return true if data is ready to Recv.
		 */
		bool Select(double timeout);

		bool Accepted() const { return accepted; }
		void MarkAccepted() { accepted = true; }

		virtual bool Connected() const;
		IPAddress GetRemoteAddr() const;

		/**
		 * Gets a number that identifies this client for the life of the server; unlike client ids, it is never reused.
		 */
		std::uint64_t Serial() const { return this->serial; }

		virtual void Close(bool force = false);

		std::time_t ConnectTime() const;

		virtual ~Client();

	// TODO: Separate Socket type
	friend class Server;
};

/**
 * Generic TCP server class.
 */
class Server
{
	public:
		enum State
		{
			/**
			 * There was an error preparing the server.
			 */
			Invalid,

			/**
			 * Newly created server, not listening yet.
			 */
			Created,

			/**
			 * Server has been bound to a port but is not yet listening.
			 */
			Bound,

			/**
			 * Server is listening and is ready to accept clients.
			 */
			Listening
		};

	private:
		struct impl_;

		impl_ *impl;

	protected:
		virtual Client *ClientFactory(const Socket &sock) { return new Client(sock, this); }

		/**
		 * The address the server will listen on.
		 */
		IPAddress address;

		/**
		 * The port the server will listen on in host order.
		 */
		uint16_t port;

		/**
		 * The port the server will listen on in network order.
		 */
		uint16_t portn;

		/**
		 * Current server state.
		 * @sa State
		 */
		State state;

		/**
		 * Maximum number of connections the server will hold at one time.
		 */
		unsigned int maxconn;

	public:
		/**
		 * List of connected clients.
		 */
		std::list<Client *> clients;

		/**
		 * Initializes the Server.
		 */
		Server();

		/**
		 * Initializes the Server and binds to the specified address and port.
		 * @param addr Address to bind to
		 * @param port Port number to bind to
		 */
		Server(const IPAddress &addr, uint16_t port);

		/**
		 * Bind the Server to the specified address and port.
		 * Once this succeeds you should call Listen().
		 * @param addr Address to bind to.
		 * @param port Port number to bind to.
		 * @throw Socket_BindFailed
		 */
		void Bind(const IPAddress &addr, uint16_t port);

		/**
		 * Bind the Server to the specified address and port.
		 * @param maxconn Maximum number of clients to have at one time.
		 * @param backlog Number of connections to keep in the queue.
		 * @throw Socket_ListenFailed
		 */
		void Listen(int maxconn, int backlog = 10);

		/**
		 * Close the server socket (previously bound with Bind)
		 */
		void Close();

		/**
		 * Check for new connection requests.
		 * @return NULL if there are no pending connections, a pointer to the Client otherwise.
		 */
		Client *Poll();

		/**
		 * Check clients for incoming data and errors, and sends data waiting in their send buffers.
		 * @param timeout Max number of seconds to block for
		 * @throw Socket_SelectFailed
		 * @throw Socket_Exception
		 * @return Returns a list of clients that have data ready to Recv.
		 */
		std::vector<Client *> *Select(double timeout);

		/**
		 * Destroys any dead clients, should be called periodically.
		 * All pointers to Client objects from this Server should be considered invalid after execution.
		 */
		void BuryTheDead();

		State State() const
		{
			return this->state;
		}

		int Connections() const
		{
			return this->clients.size();
		}

		int MaxConnections() const
		{
			return this->maxconn;
		}

		virtual ~Server();
};


#endif // SOCKET_HPP_INCLUDED
