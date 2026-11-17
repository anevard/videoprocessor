#pragma once

// A profile change VP's renderer kept back (ledger A356, the `never` gap;
// operator, 2026-11-17: "we should close it if it is not a big code change").
// Under his hand-typed diagnostic key `profile_update_mode: never` the VP
// renderer takes every change it can apply live and keeps its current program
// for one that needs a rebuild: its ApplyApplicationState answers false
// ("retained current renderer settings because profile_update_mode=never" in
// vp.log). The profile runtime holds the new selection all the same, so a
// source plugin's pick was answered OK, and a PATGEN switch read on, though
// neither reached the picture.
//
// ApplyUnifiedProfileSnapshot now says whether the renderer took its
// snapshot. A change it defers (a renderer transition, no renderer yet)
// counts as taken: the next renderer is built from the runtime's snapshot.
// Only the VP renderer answers false for a change it keeps back; a DirectShow
// renderer with no ApplyApplicationState of its own answers false for every
// snapshot (IRenderer's default), so its answer is never read as kept back.
//
// What VP does then, decided here so the host tests cover it; each case is
// logged:
//   - a source plugin's pick (select_profile with a profile): answered
//     VP_SOURCE_ERROR_STATE, and every pick the plugin made is put back
//     (ReleaseTemporaryProfiles), so the runtime names what the renderer shows
//     again; PATGEN refuses the leg ("VP refused profile ...").
//   - its release (select_profile with none): released all the same, there
//     being nothing earlier to go back to; answered VP_SOURCE_ERROR_STATE, the
//     renderer keeping the picks' settings until it takes a later change or
//     rebuilds.
//   - a PATGEN switch's copy (Low latency, a chart's 1:1 set-aside or its
//     crop): a change that turns it on goes back to what it was, with Low
//     latency's queue, which VP applies itself before the renderer answers; a
//     change that turns it off stays (no loop, and no other input inherits a
//     switch), the renderer keeping the copy's settings until it takes a later
//     change or rebuilds.

#include "vp_source_plugin.h"

namespace SourcePluginKeptBack
{
	// The answer to a source plugin's pick or release that the profile runtime
	// accepted. changed: the runtime's snapshot changed; keptBack: the
	// renderer kept that snapshot back; putBack: release every pick the plugin
	// made.
	struct Answer
	{
		int32_t result;
		bool putBack;
	};
	inline Answer ForSelection(bool release, bool changed, bool keptBack)
	{
		if (!changed || !keptBack) return { VP_SOURCE_OK, false };
		return { VP_SOURCE_ERROR_STATE, !release };
	}

	// Whether a PATGEN switch's change goes back to what it was.
	inline bool PutBack(bool turnsOn, bool keptBack)
	{
		return turnsOn && keptBack;
	}
}
