#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

// Overlay-only VP Renderer tone-mapping controls (anevard/dtm-tuning).
//
// dtm_tuning.ini sits beside VideoProcessor.exe. The exe writes it on a
// SetDtmTuning request; the renderer DLL reads it inside its own settings load,
// because the DLL links its own copy of this library and never sees exe state.
// A missing file, mode = stock or a missing active preset all mean stock: no
// libplacebo field is touched. Diagnostics go to logs\vp-dtm.log, never vp.log.
namespace DtmTuning
{
	enum class Key : size_t
	{
		KneeAdaptation,
		KneeMinimum,
		KneeMaximum,
		KneeDefault,
		SlopeTuning,
		SlopeOffset,
		SplineContrast,
		KneeOffset,
		ReinhardContrast,
		Percentile,
		SmoothingPeriod,
		SceneThresholdLow,
		SceneThresholdHigh,
		BlackCutoff,
		Count
	};
	constexpr size_t KeyCount = static_cast<size_t>(Key::Count);
	constexpr size_t MaximumIniBytes = 64 * 1024;
	constexpr size_t MaximumPresetNameLength = 32;

	struct KeySpec
	{
		const char* name;
		double defaultValue;
		double minimum;
		bool minimumExclusive;
		double maximum;
		bool maximumExclusive;
		bool peakDetection;
	};

	// libplacebo's documented defaults and ranges (tone_mapping.h and
	// shaders/colorspace.h of the vendored release). libplacebo gives the peak
	// keys no upper bound; they are capped at their physical ceiling instead.
	const std::array<KeySpec, KeyCount>& Keys();

	struct Settings
	{
		bool tuned = false;
		std::string preset;
		std::array<bool, KeyCount> present{};
		std::array<double, KeyCount> values{};

		bool Has(Key key) const { return present[static_cast<size_t>(key)]; }
		float Value(Key key) const
		{
			return static_cast<float>(values[static_cast<size_t>(key)]);
		}
		// Stable text of everything that reaches libplacebo; "stock" when not tuned.
		std::string Fingerprint() const;
		// "stock" or "tuned (name)".
		std::string OsdLabel() const;
	};

	struct ParseResult
	{
		Settings settings;
		std::vector<std::string> messages;
	};

	bool IsValidPresetName(const std::string& name);
	ParseResult Parse(const std::string& text);

	std::wstring IniPath();
	// False with an empty text when the file is absent or unreadable; the
	// message says which.
	bool ReadIni(std::string& text, std::string& message);
	ParseResult Load();
	bool WriteIni(const std::string& text, std::string& error);

	// One line per key: "key <name> <default> <min> <max> <effective|->",
	// then "mode", "preset" and "message" lines, for the laptop tuner.
	std::string DescribeForTuner(const ParseResult& result);

	// Open, append one line and close, with shared write, so the exe and the
	// renderer DLL can both log without either holding the file.
	void Log(const char* module, const std::string& message);
	// Logs the parse outcome: a summary line, then each message.
	void LogResult(const char* module, const ParseResult& result);
	// Logs the parse outcome only when it differs from this module's last one,
	// so a source change that re-reads an unchanged INI stays silent.
	void LogIfChanged(const char* module, const ParseResult& result);
}
