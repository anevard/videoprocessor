#pragma once

// VP's main window's side of a source plugin's version 6 host calls
// (vp_source_plugin.h, SourcePluginHostServices.h). Readback and present
// waits go straight to the renderer DLL (LibplaceboPluginVideoRenderer); the
// profile calls belong to the main window's profile runtime, so each is
// posted to its GUI thread and waited for, as a Config RPC request is. The
// plugin's own thread blocks; VP's GUI thread never does.

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

#include <source_plugin/SourcePluginHostServices.h>
#include <vprenderer/LibplaceboPluginVideoRenderer.h>

class SourcePluginHostCalls final : public ISourcePluginHostServices
{
public:
	// One profile call, run on the GUI thread.
	struct Job
	{
		enum class Kind { Select, Release, Get } kind = Kind::Get;
		std::string group;
		std::string profile;
		int32_t result = VP_SOURCE_ERROR;
		std::string profiles;    // Get's answer
		bool done = false;
	};
	using Runner = std::function<void(Job&)>;

	// The GUI thread waits at most this long to be reached (the Config
	// RPC's own limit).
	static constexpr uint32_t GuiWaitMs = 15000;

	// GUI thread: posts message (wParam = the job's id) to window from now.
	void Start(HWND window, UINT message)
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_window = window;
		m_message = message;
	}

	// GUI thread, when VP stops: nothing more is posted; every waiter
	// returns VP_SOURCE_ERROR_STATE.
	void Stop()
	{
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			m_window = nullptr;
			for (auto& job : m_jobs)
			{
				job.second->result = VP_SOURCE_ERROR_STATE;
				job.second->done = true;
			}
			m_jobs.clear();
		}
		m_changed.notify_all();
	}

	// GUI thread, on the message: runs the job, if its waiter still waits.
	void Run(uint64_t id, const Runner& run)
	{
		std::shared_ptr<Job> job;
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			const auto found = m_jobs.find(id);
			if (found == m_jobs.end())
				return;
			job = found->second;
			m_jobs.erase(found);
		}
		run(*job);
		{
			std::lock_guard<std::mutex> lock(m_mutex);
			job->done = true;
		}
		m_changed.notify_all();
	}

	int32_t Readback(uint64_t sourceFrame, const vp_source_rect& rect,
		uint32_t flags, uint32_t timeoutMs, vp_source_readback& out) override
	{
		return LibplaceboPluginVideoRenderer::SourceReadback(sourceFrame, rect, flags, timeoutMs, out);
	}

	int32_t WaitPresented(uint64_t sourceFrame, uint32_t timeoutMs,
		vp_source_presented& out) override
	{
		return LibplaceboPluginVideoRenderer::SourceWaitPresented(sourceFrame, timeoutMs, out);
	}

	int32_t SelectProfile(const char* groupUtf8, const char* profileUtf8) override
	{
		auto job = std::make_shared<Job>();
		job->kind = profileUtf8 ? Job::Kind::Select : Job::Kind::Release;
		job->group = groupUtf8 ? groupUtf8 : "";
		job->profile = profileUtf8 ? profileUtf8 : "";
		return Call(job);
	}

	int32_t GetProfile(const char* groupUtf8, std::string& profiles) override
	{
		auto job = std::make_shared<Job>();
		job->kind = Job::Kind::Get;
		job->group = groupUtf8;
		const int32_t result = Call(job);
		if (result == VP_SOURCE_OK)
			profiles = job->profiles;
		return result;
	}

private:
	int32_t Call(const std::shared_ptr<Job>& job)
	{
		std::unique_lock<std::mutex> lock(m_mutex);
		if (!m_window)
			return VP_SOURCE_ERROR_STATE;
		const uint64_t id = ++m_nextId;
		m_jobs[id] = job;
		if (!::PostMessage(m_window, m_message, static_cast<WPARAM>(id), 0))
		{
			m_jobs.erase(id);
			return VP_SOURCE_ERROR;
		}
		if (!m_changed.wait_for(lock, std::chrono::milliseconds(GuiWaitMs), [&] { return job->done; }))
		{
			// Not reached: never run (a job already running finishes alone).
			m_jobs.erase(id);
			return VP_SOURCE_ERROR_TIMEOUT;
		}
		return job->result;
	}

	std::mutex m_mutex;
	std::condition_variable m_changed;
	HWND m_window = nullptr;
	UINT m_message = 0;
	uint64_t m_nextId = 0;
	std::map<uint64_t, std::shared_ptr<Job>> m_jobs;
};
