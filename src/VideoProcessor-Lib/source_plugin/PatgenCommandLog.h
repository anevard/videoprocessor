#pragma once

// TEMPORARY, REMOVE ONCE FOUND (ledger A219 (1), operator 2026-11-07: "once
// we discover the problem, all those extra logging lines should be removed").
// On build 16 a valid number typed into a Patterns page box, then Enter, did
// nothing on the rig, with Config on another PC. VP logged only refusals, so
// whether the command arrived, was accepted or never came was unknown. VP's
// configuration server now logs every Patterns page command it answers: the
// sending PC, the command and VP's answer, timeouts included. Once the cause
// is found, delete this file and, in ConfigurationRpcServer.cpp, its include,
// LogPatternCommand and that function's one call in HandleClient (all marked
// TEMPORARY).
//
// Not logged: the page's once-a-second "pattern.chart_showing" and
// "pattern.raw_state" polls (PatgenChartShowing.h, PatgenRawOutput.h), which
// would bury the commands.

#include <string>

namespace PatgenCommandLog
{
	constexpr const char* Prefix = "pattern.";
	constexpr const char* Poll = "pattern.chart_showing";
	constexpr const char* RawPoll = "pattern.raw_state";

	// The RunAction names to log: every "pattern.<command>" but the polls.
	inline bool Logged(const std::string& action)
	{
		return action.rfind(Prefix, 0) == 0 && action != Poll && action != RawPoll;
	}

	// One vp.log line, e.g.
	//   Patterns page command from 192.168.1.20: "sdr_range 0 50" accepted
	//   Patterns page command from 192.168.1.20: "sdr_range 0 110" NOT applied: <VP's answer>
	inline std::string Line(const std::string& from, const std::string& action,
		bool accepted, const std::string& answer)
	{
		std::string line = "Patterns page command from " + (from.empty() ? std::string("an unknown PC") : from) +
			": \"" + action.substr(std::string(Prefix).size()) + "\"";
		if (accepted) return line + " accepted";
		return line + " NOT applied: " + (answer.empty() ? std::string("no answer") : answer);
	}
}
