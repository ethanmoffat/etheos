
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#ifndef CONSOLE_HPP_INCLUDED
#define CONSOLE_HPP_INCLUDED

#include "fwd/console.hpp"

#include <cstdarg>
#include <string>
#include <mutex>

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
	static struct RotationProperties
	{
		bool enabled;
		size_t bytes_per_file;
		unsigned interval_in_seconds;
		std::string target_directory;
		size_t file_limit;
	} rotation_properties;

	static std::mutex output_lock, error_lock;

	static size_t bytes_written[2];
	static double first_write_time[2];
	static bool OutputSuppressed;

	static inline void Init(Stream i);
	static inline void SetTextColor(Stream stream, Color color, bool bold);
	static inline void ResetTextColor(Stream stream);

	static inline void GenericOut(const std::string& prefix, Stream stream, Color color, bool bold, const char * format, va_list args);

	static inline void DeleteOldestIfNeeded(Stream stream);

public:
	static bool Styled[2];

	static void Out(const char* f, ...);
	static void Wrn(const char* f, ...);
	static void Err(const char* f, ...);
	static void Dbg(const char* f, ...);

	static void SuppressOutput(bool suppress);

	static void SetLog(Stream stream, const std::string& fileName);
	static void SetRotation(size_t bytesPerFile, unsigned interval, const std::string& directory, size_t fileLimit);
	static bool TryGetNextRotatedLogFileName(Stream stream, std::string& file_name);
};

#endif // CONSOLE_HPP_INCLUDED
