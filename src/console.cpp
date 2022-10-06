
/* $Id$
 * EOSERV is released under the zlib license.
 * See LICENSE.txt for more info.
 */

#include "console.hpp"
#include "timer.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <vector>
#include <string>

#include "platform.h"

#ifdef WIN32
#include "eoserv_windows.h"
#endif // WIN32

namespace fs = std::filesystem;

bool Console::Styled[2] = { true, true };

size_t Console::bytes_written[2] = { 0, 0 };
double Console::first_write_time[2] = { Timer::GetTime(), Timer::GetTime() };

bool Console::OutputSuppressed = false;

Console::RotationProperties Console::rotation_properties = { 0 };

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

void Console::GenericOut(const std::string& prefix, Stream stream, Color color, bool bold, const char * format, va_list args)
{
	const size_t BUFFER_SIZE = 4096;

	if (Styled[stream])
		SetTextColor(stream, color, bold);

	static char formatted[BUFFER_SIZE] = {0};
	std::vsnprintf(formatted, BUFFER_SIZE, (std::string("[" + prefix + "] ") + format + "\n").c_str(), args);
	bytes_written[stream] += strnlen_s(formatted, BUFFER_SIZE);

	std::fprintf((stream == STREAM_OUT) ? stdout : stderr, formatted);

	if (Styled[stream])
		ResetTextColor(stream);

	if (rotation_properties.enabled)
	{
		auto is_over_size = rotation_properties.bytes_per_file && bytes_written[stream] >= rotation_properties.bytes_per_file;
		auto is_over_time = rotation_properties.interval_in_seconds && (Timer::GetTime() - first_write_time[stream]) >= rotation_properties.interval_in_seconds;

		if (is_over_size || is_over_time)
		{
			std::string next_log;
			if (TryGetNextRotatedLogFileName(stream, next_log))
			{
				bytes_written[stream] = 0;
				first_write_time[stream] = Timer::GetTime();

				SetLog(stream, next_log);
			}
		}
	}
}

void Console::Out(const char* f, ...)
{
	if (OutputSuppressed) return;

	va_list args;
	va_start(args, f);
	GenericOut("   ", STREAM_OUT, COLOR_GREY, true, f, args);
	va_end(args);
}

void Console::Wrn(const char* f, ...)
{
	if (OutputSuppressed) return;

	va_list args;
	va_start(args, f);
	GenericOut("WRN", STREAM_OUT, COLOR_YELLOW, true, f, args);
	va_end(args);
}

void Console::Err(const char* f, ...)
{
	if (OutputSuppressed)
		return;

	if (!Styled[STREAM_ERR])
	{
		va_list args;
		va_start(args, f);
		GenericOut("ERR", STREAM_OUT, COLOR_RED, true, f, args);
		va_end(args);
	}

	va_list args;
	va_start(args, f);
	GenericOut("ERR", STREAM_ERR, COLOR_RED, true, f, args);
	va_end(args);
}

void Console::Dbg(const char* f, ...)
{
	if (OutputSuppressed) return;

	va_list args;
	va_start(args, f);
	GenericOut("DBG", STREAM_OUT, COLOR_GREY, true, f, args);
	va_end(args);
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

		DeleteOldestIfNeeded(stream);
	}
}

void Console::SetRotation(size_t bytesPerFile, unsigned interval, const std::string& directory, size_t fileLimit)
{
	rotation_properties.enabled = true;
	rotation_properties.bytes_per_file = bytesPerFile;
	rotation_properties.interval_in_seconds = interval;
	rotation_properties.target_directory = directory;
	rotation_properties.file_limit = fileLimit;

	std::filesystem::create_directories(rotation_properties.target_directory);
}

// https://stackoverflow.com/a/62412605/2562283
template <typename TP>
time_t to_time_t(TP tp) {
  using namespace std::chrono;
  auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now() + system_clock::now());
  return system_clock::to_time_t(sctp);
}

void Console::DeleteOldestIfNeeded(Stream stream)
{
	if (!rotation_properties.enabled)
		return;

	auto pathComponent = stream == STREAM_OUT ? "stdout" : "stderr";

	std::vector<std::pair<time_t, fs::directory_entry>> filesByWriteTime;
	for (const auto& entry : fs::directory_iterator(rotation_properties.target_directory))
	{
		auto fileName = entry.path().filename().string();
		if (entry.is_regular_file() && fileName.find(pathComponent) != std::string::npos)
		{
			auto time = to_time_t(entry.last_write_time());
			filesByWriteTime.push_back(std::make_pair(time, entry));
		}
	}

	if (filesByWriteTime.size() > rotation_properties.file_limit)
	{
		std::sort(filesByWriteTime.begin(), filesByWriteTime.end(),
			[] (std::pair<time_t, fs::directory_entry> a, std::pair<time_t, fs::directory_entry> b)
			{
				return a.first < b.first;
			});
		fs::remove(filesByWriteTime.front().second);
	}
}

bool Console::TryGetNextRotatedLogFileName(Stream stream, std::string& file_name)
{
	if (!rotation_properties.enabled)
		return false;

	time_t raw_time;
	time(&raw_time);
	const tm * time_info = localtime(&raw_time);

	std::string stream_text(stream == STREAM_OUT ? "stdout" : "stderr");

	char buf[256] = { 0 };
	snprintf(buf, 256, "%s/%s-%04d.%02d.%02d-%02d.%02d.%02d.log",
		rotation_properties.target_directory.c_str(),
		stream_text.c_str(),
		time_info->tm_year + 1900,
		time_info->tm_mon + 1,
		time_info->tm_mday,
		time_info->tm_hour,
		time_info->tm_min,
		time_info->tm_sec);

	file_name = std::string(buf);
	return true;
}
