/*
 * Copyright(C) 2021 Dennis Fleurbaaij <mail@dennisfleurbaaij.com>
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

#include <map>
#include <cstdint>
#include <string>
#include <vector>


class ConfigFile
{
public:
	static constexpr const char* DEFAULT_FILENAME = "VideoProcessor.cfg";
	static constexpr const char* RENDERER_FILENAME = "VideoProcessorRenderer.cfg";

	enum class ReadPolicy { ReuseUnchanged, Fresh };
	// Reuse only while file identity, size, write time and change time match.
	// Explicit reload validation uses Fresh to independently sample the file.
	bool Load(const std::string& filename = DEFAULT_FILENAME,
		ReadPolicy policy = ReadPolicy::ReuseUnchanged);
	// Parse a sibling staged candidate while resolving relative resources
	// against the configuration path that will own it after commit.
	bool LoadStagedCandidate(const std::string& stagedPath,
		const std::string& logicalPath,
		ReadPolicy policy = ReadPolicy::Fresh);
	bool IsLoaded() const { return m_loaded; }
	const std::string& GetLoadedPath() const { return m_loadedPath; }
    uint64_t GetContentIdentity() const { return m_contentIdentity; }
	const std::vector<std::string>& GetWarnings() const { return m_warnings; }
	// Values the Quality migration could not carry; informational, never a syntax error.
	const std::vector<std::string>& GetMigrationWarnings() const { return m_migrationWarnings; }
	bool HasSection(const std::string& section) const;
	bool TryGetString(const std::string& section, const std::string& key, std::string& value) const;
	bool TryGetBool(const std::string& section, const std::string& key, bool& value) const;
	const std::map<std::string, std::string>* GetSectionValues(const std::string& section) const;
	std::vector<std::string> GetSectionNames() const;
	// Sets one value in this object only; the file is never written. For a
	// copy VP hands its renderer while the pattern generator's "Crop exactly
	// to the chart" switch is on (PatgenNoCrop.h). A missing section is added
	// at the end of the order, or at its front when first is true (a group
	// with no root section takes its first named profile as its baseline).
	// Header-only: no member is added, so the renderer DLL's own ConfigFile
	// copy reads the object unchanged.
	void SetValueInMemory(const std::string& section, const std::string& key,
		const std::string& value, bool first = false)
	{
		const std::string name = NormalizeName(section);
		if (m_sections.find(name) == m_sections.end())
		{
			if (first) m_sectionOrder.insert(m_sectionOrder.begin(), name);
			else m_sectionOrder.push_back(name);
		}
		m_sections[name][NormalizeName(key)] = value;
	}

	static std::string NormalizeName(const std::string& value);
	static std::string Trim(const std::string& value);
	static bool TryParseIndexedKey(
		const std::string& key,
		const std::string& prefix,
		unsigned int& oneBasedIndex);
	static bool TryParseCommandLineOption(
		const std::vector<std::string>& arguments,
		const std::string& option,
		std::string& value,
		std::string& error);
	static bool TryResolveRendererConfigSelection(
		const std::vector<std::string>& arguments,
		std::string& filename,
		bool& explicitSelection,
		bool& compatibilityOverride,
		std::string& error);
	// The GUI resolves the renderer path once and passes it across the optional
	// plugin ABI. Each module has its own statically linked ConfigFile copy, so
	// this pins every later renderer load to the core-selected file.
	static void SetRendererConfigurationPath(const std::string& path);
	static std::string GetRendererConfigurationPath();
	static uint64_t GetLoadCount();

private:
	std::map<std::string, std::map<std::string, std::string>> m_sections;
	std::vector<std::string> m_sectionOrder;
	std::vector<std::string> m_warnings;
	std::vector<std::string> m_migrationWarnings;
	std::string m_loadedPath;
	bool m_loaded = false;
    uint64_t m_contentIdentity = 0;
};
