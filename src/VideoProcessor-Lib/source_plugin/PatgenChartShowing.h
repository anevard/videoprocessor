#pragma once

// The Config Patterns page's question on its status tick (ledger A202,
// operator: "Ride the existing poll"): is the chart the page put up still on
// screen? Asked only while the page has a chart up, on the poll Config
// already runs, and no more once the answer is no.
//
// Config sends RunAction "pattern.chart_showing"; VP passes "chart_showing"
// to the pattern generator, which answers VP_SOURCE_OK, or
// VP_SOURCE_ERROR_STATE once ColourSpace has drawn a patch, blanked or
// dropped its link since the last chart command. VP refuses the action with
// ColourSpaceHasScreen for that answer only; Config matches that text and
// sets every chart drop-down back to "Pick a chart". Any other answer (an
// older generator that does not know the question) leaves the page alone.

#include <cstdint>
#include <source_plugin/vp_source_plugin.h>

namespace PatgenChartShowing
{
	constexpr const char* Command = "chart_showing";
	constexpr const char* ColourSpaceHasScreen = "ColourSpace has the screen.";

	inline bool ColourSpaceHasTheScreen(int32_t answer)
	{
		return answer == VP_SOURCE_ERROR_STATE;
	}
}
