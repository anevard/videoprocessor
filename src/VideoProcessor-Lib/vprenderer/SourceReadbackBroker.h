#pragma once

// A source plugin's readback and present waits (source plugin interface
// version 6, vp_source_plugin.h): the plugin's thread asks, the render
// thread answers. One broker per renderer DLL, shared by every renderer it
// creates, so a wait outlives a renderer restart and needs no renderer
// pointer. The renderer DLL exports it (LibplaceboRendererPluginApi.h); the
// executable reaches it through LibplaceboPluginVideoRenderer.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <source_plugin/vp_source_plugin.h>

class SourceReadbackBroker
{
public:
	// The plugin's thread. Waits until the render thread read a frame whose
	// counter is at least sourceFrame.
	int32_t Readback(uint64_t sourceFrame, const vp_source_rect& rect, uint32_t flags,
		uint32_t timeoutMs, vp_source_readback& out)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		if (m_readbackPending)
			return VP_SOURCE_ERROR_STATE;
		m_request = Request{ ++m_requestId, sourceFrame, rect, flags };
		m_readbackPending = true;
		m_readbackDone = false;
		const bool done = m_changed.wait_for(lock, std::chrono::milliseconds(timeoutMs),
			[this] { return m_readbackDone; });
		// A request the render thread is still reading when its waiter gives
		// up is completed into nothing (Complete checks the id).
		m_readbackPending = false;
		if (!done)
			return VP_SOURCE_ERROR_TIMEOUT;
		m_readbackDone = false;
		const uint32_t size = out.struct_size;
		std::memcpy(&out, &m_readback, size < sizeof(m_readback) ? size : sizeof(m_readback));
		out.struct_size = size;
		return m_readbackResult;
	}

	// The plugin's thread. Waits for a present, made after this call began,
	// of a frame whose counter is at least sourceFrame.
	int32_t WaitPresented(uint64_t sourceFrame, uint32_t timeoutMs, vp_source_presented& out)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		const uint64_t after = m_presentSerial;
		const bool seen = m_changed.wait_for(lock, std::chrono::milliseconds(timeoutMs),
			[&] { return m_presentSerial > after && m_presented.source_frame >= sourceFrame; });
		if (!seen)
			return VP_SOURCE_ERROR_TIMEOUT;
		const uint32_t size = out.struct_size;
		std::memcpy(&out, &m_presented, size < sizeof(m_presented) ? size : sizeof(m_presented));
		out.struct_size = size;
		return VP_SOURCE_OK;
	}

	// The render thread, per rendered frame: a nonzero id (once per request)
	// when a readback waits for this frame; the caller reads it, then
	// Complete()s that id after its present.
	uint64_t Take(uint64_t sourceFrame, vp_source_rect& rect, uint32_t& flags)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (!m_readbackPending || m_takenId == m_request.id || sourceFrame < m_request.sourceFrame)
			return 0;
		m_takenId = m_request.id;
		rect = m_request.rect;
		flags = m_request.flags;
		return m_takenId;
	}

	// The render thread: request id's answer (result VP_SOURCE_OK or a
	// failure), after the present it rode. Dropped when its waiter gave up.
	void Complete(uint64_t id, int32_t result, const vp_source_readback& readback)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			if (!m_readbackPending || id != m_request.id)
				return;
			m_readbackPending = false;
			m_readbackDone = true;
			m_readbackResult = result;
			m_readback = readback;
		}
		m_changed.notify_all();
	}

	// The render thread, after every successful present.
	void Presented(const vp_source_presented& presented)
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_presented = presented;
			++m_presentSerial;
		}
		m_changed.notify_all();
	}

private:
	struct Request
	{
		uint64_t id = 0;
		uint64_t sourceFrame = 0;
		vp_source_rect rect = {};
		uint32_t flags = 0;
	};

	std::mutex m_mutex;
	std::condition_variable m_changed;
	uint64_t m_requestId = 0;
	Request m_request;
	uint64_t m_takenId = 0;           // the request the render thread took last
	bool m_readbackPending = false;   // a waiter wants a readback
	bool m_readbackDone = false;      // its answer waits for the waiter
	int32_t m_readbackResult = VP_SOURCE_ERROR;
	vp_source_readback m_readback = {};
	uint64_t m_presentSerial = 0;
	vp_source_presented m_presented = {};
};
