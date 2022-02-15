
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "console.hpp"

#include <cstdio>
#include <ctime>
#include <string>

#include "platform.h"

#ifdef WIN32
#include "eoserv_windows.h"
#endif // WIN32

bool Console::Styled[2] = { true, true };
size_t Console::BytesWritten[2] = { 0 };
bool Console::OutputSuppressed = false;

#ifdef WIN32

static HANDLE Handles[2];

void Console::Init(Stream i)
{
	if (!Handles[i])
	{
		Handles[i] = GetStdHandle((i == STREAM_OUT) ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE);
	}
}

void Console::SetTextColor(Stream stream, Color color, bool bold)
{
	Init(stream);
	SetConsoleTextAttribute(Handles[stream], color + (bold ? 8 : 0));
}

void Console::ResetTextColor(Stream stream)
{
	Init(stream);
	SetConsoleTextAttribute(Handles[stream], COLOR_GREY);
}

#else // WIN32

static const int ansi_color_map[] = {0, 4, 2, 6, 1, 5, 3, 7, 0};

void Console::SetTextColor(Stream stream, Color color, bool bold)
{
	char command[8] = {27, '[', '1', ';', '3', '0', 'm', 0};

	if (bold)
	{
		command[5] += ansi_color_map[static_cast<int>(color)];
	}
	else
	{
		for (int i = 2; i < 6; ++i)
			command[i] = command[i + 2];

		command[3] += ansi_color_map[static_cast<int>(color)];
	}

	std::fputs(command, (stream == STREAM_OUT) ? stdout : stderr);
}

void Console::ResetTextColor(Stream stream)
{
	char command[5] = {27, '[', '0', 'm', 0};
	std::fputs(command, (stream == STREAM_OUT) ? stdout : stderr);
}

#endif // WIN32

size_t Console::GenericOut(const std::string& prefix, Stream stream, Color color, bool bold, const char * format, va_list args)
{
	const size_t BUFFER_SIZE = 4096;
	size_t bytesWritten = 0;

	if (Styled[stream])
		SetTextColor(stream, color, bold);

	static char formatted[BUFFER_SIZE] = {0};
	std::vsnprintf(formatted, BUFFER_SIZE, (std::string("[" + prefix + "] ") + format + "\n").c_str(), args);
	bytesWritten = strnlen_s(formatted, BUFFER_SIZE);

	std::fprintf((stream == STREAM_OUT) ? stdout : stderr, formatted);

	if (Styled[stream])
		ResetTextColor(stream);

	return bytesWritten;
}

void Console::Out(const char* f, ...)
{
	if (OutputSuppressed) return;

	va_list args;
	va_start(args, f);
	size_t bytesWritten = GenericOut("   ", STREAM_OUT, COLOR_GREY, true, f, args);
	va_end(args);

	BytesWritten[STREAM_OUT] += bytesWritten;
}

void Console::Wrn(const char* f, ...)
{
	if (OutputSuppressed) return;

	va_list args;
	va_start(args, f);
	size_t bytesWritten = GenericOut("WRN", STREAM_OUT, COLOR_YELLOW, true, f, args);
	va_end(args);

	BytesWritten[STREAM_OUT] += bytesWritten;
}

void Console::Err(const char* f, ...)
{
	if (OutputSuppressed)
		return;

	if (!Styled[STREAM_ERR])
	{
		va_list args;
		va_start(args, f);

		size_t bytesWritten = GenericOut("ERR", STREAM_OUT, COLOR_RED, true, f, args);
		BytesWritten[STREAM_OUT] += bytesWritten;

		va_end(args);
	}

	va_list args;
	va_start(args, f);

	size_t bytesWritten = GenericOut("ERR", STREAM_ERR, COLOR_RED, true, f, args);
	BytesWritten[STREAM_ERR] += bytesWritten;

	va_end(args);
}

void Console::Dbg(const char* f, ...)
{
	if (OutputSuppressed) return;

	va_list args;
	va_start(args, f);
	size_t bytesWritten = GenericOut("DBG", STREAM_OUT, COLOR_GREY, true, f, args);
	va_end(args);

	BytesWritten[STREAM_OUT] += bytesWritten;
}

void Console::SuppressOutput(bool suppress)
{
	OutputSuppressed = suppress;
}

void Console::SetLog(Stream stream, const std::string& fileName)
{
	const size_t TIME_BUF_SIZE = 256;
	std::time_t rawtime;
	char timestr[TIME_BUF_SIZE];
	std::time(&rawtime);
	std::strftime(timestr, TIME_BUF_SIZE, "%c", std::localtime(&rawtime));

	FILE * outStream = nullptr;
	switch (stream)
	{
		case STREAM_OUT:
			outStream = stdout;
			break;
		case STREAM_ERR:
			outStream = stderr;
			break;
		default:
			throw std::exception("Invalid stream for setting log");
	}

	if (!fileName.empty() && fileName.compare("-") != 0)
	{
		const char * targetStreamName = (stream == STREAM_OUT ? "stdout" : "stderr");
		Console::Out("Redirecting %s to '%s'...", targetStreamName, fileName.c_str());
		if (!std::freopen(fileName.c_str(), "a", outStream))
		{
			Console::Err("Failed to redirect %s.", targetStreamName);
		}
		else
		{
			Console::Styled[stream] = false;
			std::fprintf(outStream, "\n\n--- %s ---\n\n", timestr);
		}

		if (std::setvbuf(outStream, 0, _IONBF, 0) != 0)
		{
			Console::Wrn("Failed to change %s buffer settings", targetStreamName);
		}
	}
}

void Console::SetRollover(size_t bytesPerFile, time_t interval, const std::string& format)
{
	// todo
}
