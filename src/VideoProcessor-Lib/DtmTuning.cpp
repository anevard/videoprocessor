#include <pch.h>

#include "DtmTuning.h"
#include "DebugLog.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <locale>
#include <mutex>
#include <sstream>

namespace DtmTuning
{
namespace
{
	std::string Trim(const std::string& value)
	{
		const size_t first = value.find_first_not_of(" \t\r\n");
		if (first == std::string::npos) return {};
		const size_t last = value.find_last_not_of(" \t\r\n");
		return value.substr(first, last - first + 1);
	}

	std::string Lower(std::string value)
	{
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}

	std::string FormatNumber(double value)
	{
		std::ostringstream stream;
		stream.imbue(std::locale::classic());
		stream.precision(9);
		stream << value;
		return stream.str();
	}

	bool ParseNumber(const std::string& text, double& value)
	{
		if (text.empty()) return false;
		std::istringstream stream(text);
		stream.imbue(std::locale::classic());
		double parsed = 0.0;
		stream >> parsed;
		if (stream.fail()) return false;
		stream >> std::ws;
		if (!stream.eof() || !std::isfinite(parsed)) return false;
		value = parsed;
		return true;
	}

	bool InRange(const KeySpec& spec, double value, double minimum, double maximum)
	{
		if (spec.minimumExclusive ? value <= minimum : value < minimum) return false;
		if (spec.maximumExclusive ? value >= maximum : value > maximum) return false;
		return true;
	}

	std::string RangeText(const KeySpec& spec, double minimum, double maximum)
	{
		return std::string(spec.minimumExclusive ? "(" : "[") +
			FormatNumber(minimum) + ", " + FormatNumber(maximum) +
			(spec.maximumExclusive ? ")" : "]");
	}

	std::wstring ExecutableDirectory()
	{
		std::vector<wchar_t> buffer(32768);
		const DWORD length = GetModuleFileNameW(
			nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
		if (length == 0 || length >= buffer.size()) return {};
		std::wstring path(buffer.data(), length);
		const size_t separator = path.find_last_of(L"\\/");
		if (separator == std::wstring::npos) return {};
		path.resize(separator);
		return path;
	}

	std::wstring Widen(const std::string& text)
	{
		if (text.empty()) return {};
		const int length = MultiByteToWideChar(CP_ACP, 0, text.data(),
			static_cast<int>(text.size()), nullptr, 0);
		if (length <= 0) return {};
		std::wstring result(static_cast<size_t>(length), L'\0');
		MultiByteToWideChar(CP_ACP, 0, text.data(),
			static_cast<int>(text.size()), &result[0], length);
		return result;
	}

	std::wstring LogPath()
	{
		// Follow whatever path DebugLog selected for vp.log in this module.
		std::wstring path = Widen(DebugLog::GetLogFilePath());
		const size_t separator = path.find_last_of(L"\\/");
		if (separator == std::wstring::npos) return L"vp-dtm.log";
		path.resize(separator + 1);
		return path + L"vp-dtm.log";
	}

	std::string Summary(const ParseResult& result)
	{
		std::string summary = "mode=" + std::string(result.settings.tuned ?
			"tuned" : "stock");
		if (result.settings.tuned)
		{
			summary += " preset=" + result.settings.preset + " applied=";
			bool any = false;
			for (size_t index = 0; index < KeyCount; ++index)
			{
				if (!result.settings.present[index]) continue;
				summary += std::string(any ? "," : "") + Keys()[index].name + "=" +
					FormatNumber(result.settings.values[index]);
				any = true;
			}
			if (!any) summary += "none";
		}
		return summary;
	}
}

const std::array<KeySpec, KeyCount>& Keys()
{
	static const std::array<KeySpec, KeyCount> keys = { {
		{ "knee_adaptation",      0.4,   0.0, false,     1.0, false, false },
		{ "knee_minimum",         0.1,   0.0, true,      0.5, true,  false },
		{ "knee_maximum",         0.8,   0.5, true,      1.0, true,  false },
		// Bounded again by the effective knee_minimum and knee_maximum.
		{ "knee_default",         0.4,   0.0, false,     1.0, false, false },
		{ "slope_tuning",         1.5,   0.0, false,    10.0, false, false },
		{ "slope_offset",         0.2,   0.0, false,     1.0, false, false },
		{ "spline_contrast",      0.5,   0.0, false,     1.5, false, false },
		{ "knee_offset",          1.0,   0.5, false,     2.0, false, false },
		{ "reinhard_contrast",    0.5,   0.0, true,      1.0, true,  false },
		{ "percentile",         100.0,   0.0, false,   100.0, false, true },
		{ "smoothing_period",    20.0,   0.0, false, 10000.0, false, true },
		{ "scene_threshold_low",  1.0,   0.0, false,   100.0, false, true },
		{ "scene_threshold_high", 3.0,   0.0, false,   100.0, false, true },
		{ "black_cutoff",         1.0,   0.0, false,   100.0, false, true },
	} };
	return keys;
}

std::string Settings::Fingerprint() const
{
	if (!tuned) return "stock";
	std::ostringstream stream;
	stream.imbue(std::locale::classic());
	stream.precision(17);
	stream << "tuned|" << preset;
	for (size_t index = 0; index < KeyCount; ++index)
	{
		stream << '|';
		if (present[index]) stream << values[index];
	}
	return stream.str();
}

std::string Settings::OsdLabel() const
{
	return tuned ? "tuned (" + preset + ")" : "stock";
}

bool IsValidPresetName(const std::string& name)
{
	if (name.empty() || name.size() > MaximumPresetNameLength) return false;
	for (const unsigned char c : name)
	{
		if (!std::isalnum(c) && c != '-' && c != '_' && c != '.' && c != ' ')
			return false;
	}
	return name.front() != ' ' && name.back() != ' ';
}

ParseResult Parse(const std::string& text)
{
	ParseResult result;
	std::string mode;
	std::string active;
	// Raw key/value text of the active preset, collected after the [dtm]
	// section is known because the preset section may come first.
	struct Section
	{
		std::string name;
		std::vector<std::pair<std::string, std::string>> entries;
	};
	std::vector<Section> presets;
	std::string section;
	std::istringstream lines(text);
	std::string line;
	int lineNumber = 0;
	while (std::getline(lines, line))
	{
		++lineNumber;
		const size_t comment = line.find_first_of(";#");
		if (comment != std::string::npos) line.resize(comment);
		line = Trim(line);
		if (line.empty()) continue;
		if (line.front() == '[')
		{
			if (line.back() != ']')
			{
				result.messages.push_back("line " + std::to_string(lineNumber) +
					": malformed section header ignored");
				section = "?";
				continue;
			}
			section = Trim(line.substr(1, line.size() - 2));
			if (Lower(section).rfind("preset.", 0) == 0)
				presets.push_back({ Trim(section.substr(7)), {} });
			continue;
		}
		const size_t equals = line.find('=');
		if (equals == std::string::npos)
		{
			result.messages.push_back("line " + std::to_string(lineNumber) +
				": no '=' in entry, ignored");
			continue;
		}
		const std::string key = Lower(Trim(line.substr(0, equals)));
		const std::string value = Trim(line.substr(equals + 1));
		if (Lower(section) == "dtm")
		{
			if (key == "mode") mode = Lower(value);
			else if (key == "active") active = value;
			else
				result.messages.push_back("[dtm] unknown key '" + key + "' ignored");
		}
		else if (!presets.empty() && Lower(section).rfind("preset.", 0) == 0)
			presets.back().entries.emplace_back(key, value);
	}

	if (mode.empty() || mode == "stock")
		return result;
	if (mode != "tuned")
	{
		result.messages.push_back("mode '" + mode + "' is not stock or tuned; using stock");
		return result;
	}
	if (!IsValidPresetName(active))
	{
		result.messages.push_back("active preset name '" + active +
			"' is missing or invalid; using stock");
		return result;
	}
	const Section* preset = nullptr;
	for (const Section& candidate : presets)
		if (candidate.name == active) preset = &candidate;
	if (!preset)
	{
		result.messages.push_back("active preset '" + active +
			"' has no [preset." + active + "] section; using stock");
		return result;
	}

	Settings& settings = result.settings;
	settings.tuned = true;
	settings.preset = active;
	const auto& keys = Keys();
	for (const auto& entry : preset->entries)
	{
		size_t index = 0;
		while (index < KeyCount && entry.first != keys[index].name) ++index;
		if (index == KeyCount)
		{
			result.messages.push_back("unknown key '" + entry.first + "' ignored");
			continue;
		}
		double value = 0.0;
		if (!ParseNumber(entry.second, value) ||
			!InRange(keys[index], value, keys[index].minimum, keys[index].maximum))
		{
			result.messages.push_back(std::string(keys[index].name) + " = '" +
				entry.second + "' rejected, outside " +
				RangeText(keys[index], keys[index].minimum, keys[index].maximum) +
				"; using default " + FormatNumber(keys[index].defaultValue));
			settings.present[index] = false;
			continue;
		}
		settings.present[index] = true;
		settings.values[index] = value;
	}

	// knee_default must lie within the knees actually in effect.
	const size_t kneeDefault = static_cast<size_t>(Key::KneeDefault);
	if (settings.present[kneeDefault])
	{
		auto effective = [&](Key key)
		{
			const size_t index = static_cast<size_t>(key);
			return settings.present[index] ? settings.values[index] :
				keys[index].defaultValue;
		};
		const double minimum = effective(Key::KneeMinimum);
		const double maximum = effective(Key::KneeMaximum);
		const double value = settings.values[kneeDefault];
		if (value < minimum || value > maximum)
		{
			result.messages.push_back("knee_default = " + FormatNumber(value) +
				" rejected, outside the effective knees [" + FormatNumber(minimum) +
				", " + FormatNumber(maximum) + "]; using default " +
				FormatNumber(keys[kneeDefault].defaultValue));
			settings.present[kneeDefault] = false;
		}
	}
	return result;
}

std::wstring IniPath()
{
	const std::wstring directory = ExecutableDirectory();
	return directory.empty() ? L"dtm_tuning.ini" : directory + L"\\dtm_tuning.ini";
}

bool ReadIni(std::string& text, std::string& message)
{
	text.clear();
	const std::wstring path = IniPath();
	const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		const DWORD error = GetLastError();
		message = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ?
			"no dtm_tuning.ini; stock" :
			"dtm_tuning.ini could not be opened (error " + std::to_string(error) +
				"); stock";
		return false;
	}
	LARGE_INTEGER size{};
	bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
		size.QuadPart <= static_cast<LONGLONG>(MaximumIniBytes);
	if (ok && size.QuadPart > 0)
	{
		text.resize(static_cast<size_t>(size.QuadPart));
		DWORD read = 0;
		ok = ReadFile(file, &text[0], static_cast<DWORD>(text.size()), &read,
			nullptr) && read == text.size();
	}
	CloseHandle(file);
	if (!ok)
	{
		text.clear();
		message = "dtm_tuning.ini is unreadable or larger than " +
			std::to_string(MaximumIniBytes) + " bytes; stock";
		return false;
	}
	return true;
}

ParseResult Load()
{
	std::string text;
	std::string message;
	if (!ReadIni(text, message))
	{
		ParseResult result;
		result.messages.push_back(message);
		return result;
	}
	return Parse(text);
}

bool WriteIni(const std::string& text, std::string& error)
{
	if (text.size() > MaximumIniBytes)
	{
		error = "DTM tuning text exceeds " + std::to_string(MaximumIniBytes) + " bytes.";
		return false;
	}
	const std::wstring path = IniPath();
	const std::wstring stage = path + L".stage." +
		std::to_wstring(GetCurrentProcessId()) + L"." +
		std::to_wstring(GetTickCount64()) + L".tmp";
	const HANDLE file = CreateFileW(stage.c_str(), GENERIC_WRITE, 0, nullptr,
		CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE)
	{
		error = "Could not create the DTM tuning staging file.";
		return false;
	}
	DWORD written = 0;
	const bool wrote = text.empty() || (WriteFile(file, text.data(),
		static_cast<DWORD>(text.size()), &written, nullptr) &&
		written == text.size());
	const bool flushed = wrote && FlushFileBuffers(file);
	CloseHandle(file);
	if (!flushed || !MoveFileExW(stage.c_str(), path.c_str(),
		MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
	{
		DeleteFileW(stage.c_str());
		error = flushed ? "Could not replace dtm_tuning.ini." :
			"Could not write the DTM tuning staging file.";
		return false;
	}
	return true;
}

std::string DescribeForTuner(const ParseResult& result)
{
	std::string text;
	text += "mode " + std::string(result.settings.tuned ? "tuned" : "stock") + "\n";
	text += "preset " + result.settings.preset + "\n";
	const auto& keys = Keys();
	for (size_t index = 0; index < KeyCount; ++index)
	{
		text += std::string("key ") + keys[index].name + " " +
			FormatNumber(keys[index].defaultValue) + " " +
			FormatNumber(keys[index].minimum) + " " +
			FormatNumber(keys[index].maximum) + " " +
			(result.settings.present[index] ?
				FormatNumber(result.settings.values[index]) : "-") + "\n";
	}
	for (const std::string& message : result.messages)
		text += "message " + message + "\n";
	return text;
}

void Log(const char* module, const std::string& message)
{
	const std::wstring path = LogPath();
	const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
		OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (file == INVALID_HANDLE_VALUE) return;
	SYSTEMTIME now{};
	GetLocalTime(&now);
	char stamp[64] = {};
	sprintf_s(stamp, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] ",
		now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
		now.wMilliseconds, module ? module : "?");
	const std::string line = stamp + message + "\r\n";
	DWORD written = 0;
	WriteFile(file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
	CloseHandle(file);
}

void LogResult(const char* module, const ParseResult& result)
{
	Log(module, Summary(result));
	for (const std::string& message : result.messages)
		Log(module, message);
}

void LogIfChanged(const char* module, const ParseResult& result)
{
	std::string text = Summary(result);
	for (const std::string& message : result.messages)
		text += "; " + message;
	static std::mutex mutex;
	static std::string last;
	{
		std::lock_guard<std::mutex> guard(mutex);
		if (text == last) return;
		last = text;
	}
	LogResult(module, result);
}
}
