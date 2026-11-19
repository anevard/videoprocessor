#pragma once

// A Config Patterns page chart setting (PatternLiveKeys.h) is sent to VP
// first and saved after VP's answer. Taken, or refused only because the
// pattern generator is not the active input, it is saved: the generator
// reads it when its input starts. Any other refusal saves nothing and the
// page sets the control back, so a refused value never reaches the
// configuration file. Saving before the answer made every change a
// configuration Apply on the target VP, refused ones included.
//
// VP refuses a "pattern.<command>" with NotActiveInput, word for word, while
// another input is active; Config matches that text.

#include <string>

namespace PatgenLiveSave
{
	constexpr const char* NotActiveInput = "The pattern generator is not the active input.";

	// True when the page saves the setting after VP's answer.
	inline bool Saved(bool taken, const std::string& answer)
	{
		return taken || answer == NotActiveInput;
	}
}
