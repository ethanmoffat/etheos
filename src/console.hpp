
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef CONSOLE_HPP_INCLUDED
#define CONSOLE_HPP_INCLUDED

#include "fwd/console.hpp"

#include <cstdarg>
#include <string>

class Console
{
public:
	enum Color
	{
		COLOR_BLUE = 1,
		COLOR_GREEN = 2,
		COLOR_CYAN = 3,
		COLOR_RED = 4,
		COLOR_MAGENTA = 5,
		COLOR_YELLOW = 6,
		COLOR_GREY = 7,
		COLOR_BLACK = 8
	};

	enum Stream
	{
		STREAM_OUT,
		STREAM_ERR
	};

private:
	static size_t BytesWritten[2];
	static bool OutputSuppressed;

	static inline void Init(Stream i);
	static inline void SetTextColor(Stream stream, Color color, bool bold);
	static inline void ResetTextColor(Stream stream);

	static inline size_t GenericOut(const std::string& prefix, Stream stream, Color color, bool bold, const char * format, va_list args);

public:
	static bool Styled[2];

	static void Out(const char* f, ...);
	static void Wrn(const char* f, ...);
	static void Err(const char* f, ...);
	static void Dbg(const char* f, ...);

	static void SuppressOutput(bool suppress);

	static void SetLog(Stream stream, const std::string& fileName);
	static void SetRollover(size_t bytesPerFile, time_t interval, const std::string& format);
};

#endif // CONSOLE_HPP_INCLUDED
