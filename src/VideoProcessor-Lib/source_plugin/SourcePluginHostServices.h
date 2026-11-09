#pragma once

// What a source plugin's version 6 host calls reach (vp_source_plugin.h:
// readback, wait_presented, select_profile, get_profile). VP's main window
// owns the renderer and the profiles, so it implements them and registers
// itself here for its life; the plugin adapter (SourcePluginCaptureDevice)
// only forwards. Every call comes on a plugin's own thread and may block.

#include <atomic>
#include <cstdint>
#include <string>

#include <source_plugin/vp_source_plugin.h>

class ISourcePluginHostServices
{
public:
	virtual ~ISourcePluginHostServices() = default;

	virtual int32_t Readback(uint64_t sourceFrame, const vp_source_rect& rect,
		uint32_t flags, uint32_t timeoutMs, vp_source_readback& out) = 0;
	virtual int32_t WaitPresented(uint64_t sourceFrame, uint32_t timeoutMs,
		vp_source_presented& out) = 0;
	// profile nullptr: release every pick made through this call.
	virtual int32_t SelectProfile(const char* groupUtf8, const char* profileUtf8) = 0;
	// The profiles in force in the group, comma separated.
	virtual int32_t GetProfile(const char* groupUtf8, std::string& profiles) = 0;
};

namespace SourcePluginHostServices
{
	inline std::atomic<ISourcePluginHostServices*>& Slot()
	{
		static std::atomic<ISourcePluginHostServices*> services{ nullptr };
		return services;
	}

	// VP's main window: itself when it starts, nullptr before it goes (after
	// its capture input is removed, so no plugin thread is still calling).
	inline void Set(ISourcePluginHostServices* services)
	{
		Slot().store(services, std::memory_order_release);
	}

	inline ISourcePluginHostServices* Get()
	{
		return Slot().load(std::memory_order_acquire);
	}
}
