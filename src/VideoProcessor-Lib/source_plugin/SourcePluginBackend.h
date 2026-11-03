/*
 * Copyright(C) 2026 Andrew Nevard
 *
 * This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 3.
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License along with this program. If not, see < https://www.gnu.org/licenses/>.
 */

#pragma once

// Host and configuration boundary for source plugins (plugins/sources/*.dll),
// shaped like MagewellBackend. Without a plugin every function finds nothing
// and VP behaves exactly as before.
#include <ACaptureDeviceDiscoverer.h>
#include <string>
#include <vector>

namespace SourcePluginBackend
{
	// Discoverer for the dialog. Start() loads every valid plugin once and
	// reports it as a capture device; Stop() reports each one lost.
	CComPtr<ACaptureDeviceDiscoverer> CreateDiscoverer(ICaptureDeviceDiscovererCallback& callback);

	// True if the device is a source plugin's input (by type, not by name).
	bool IsSourcePluginDevice(ACaptureDevice* device);

	// The source plugins VP's own keys and Config act on, by the name in
	// their function table: the media player and the pattern generator.
	constexpr const char* PLAYER_PLUGIN_NAME = "VP Player";
	constexpr const char* PATTERN_PLUGIN_NAME = "VP TPG";

	// True if the device is the input of the source plugin of that name.
	bool IsSourcePluginNamed(ACaptureDevice* device, const char* nameUtf8);

	// GUI thread. Sends one player command line (e.g. "pause", "seek +10")
	// to the device's plugin. False if the device is not a source plugin;
	// a command the plugin rejects is logged and still returns true.
	// result, if given, receives the plugin's result.
	bool SendSourcePluginCommand(ACaptureDevice* device, const char* commandUtf8,
		int32_t* result = nullptr);

	// GUI thread. VP's Player shortcuts as configured, for the plugin's own
	// remote page (accelerator.key 0 = cleared). Kept, sent to the active
	// device if it is a source plugin, and sent again at every capture start.
	struct PlayerKey
	{
		std::string name;     // [shortcuts] key, e.g. "player_play_pause"
		ACCEL accelerator;
	};
	void SetPlayerKeys(const std::vector<PlayerKey>& keys, ACaptureDevice* activeDevice);

	// The "keys ..." command line last set; empty before the first SetPlayerKeys.
	const std::string& PlayerKeysCommand();

	// GUI thread, before each capture start. The display VP is fullscreen on,
	// whose desktop mode the Player's idle picture takes.
	void SetFullscreenMonitor(HMONITOR monitor);

	// That display's current desktop mode as "<w>x<h>@<num>/<den> sdr|pq",
	// pq when Windows HDR is on for it; empty if it cannot be read (the
	// plugin then uses its own fallback). why receives the reason if empty.
	std::string DesktopModeFormat(std::string& why);

	// For the configuration editor: plugin names and their inputs. Reads each
	// plugin's function table only; never creates an instance.
	void AppendDeviceNames(std::vector<std::wstring>& names);
	bool IsDeviceName(const std::wstring& deviceName);
	std::vector<std::wstring> ConnectionNames(const std::wstring& deviceName);
}
