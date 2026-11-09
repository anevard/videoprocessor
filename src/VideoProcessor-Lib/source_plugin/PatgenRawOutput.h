#pragma once

// The Config Patterns page's "Raw output: no VP processing" box (ledger
// A255). The switch is never saved and is off whenever VP or the pattern
// generator's input starts, so the page cannot read it from the config: it
// asks the generator, when the page is built and on the status tick, and
// shows the answer. Only the user's own click on the box sends "raw on" or
// "raw off"; an answer only moves the box (operator: it must not switch off
// by accident).
//
// Config sends RunAction "pattern.raw_state"; VP passes "raw_state" to the
// generator, which answers 1 while raw output is on and 0 while it is off.
// VP answers the action OK for on and refuses it with Off for off; Config
// matches that text. Any other answer (another input, an older generator)
// leaves the box as it is.

#include <cstdint>
#include <string>

namespace PatgenRawOutput
{
	constexpr const char* StateCommand = "raw_state";
	constexpr const char* Off = "Raw output is off.";

	inline bool IsOn(int32_t answer)
	{
		return answer == 1;
	}

	inline bool IsOff(int32_t answer)
	{
		return answer == 0;
	}

	// The Patterns page's other live switches, read the same way (A261):
	// "ColourSpace patches 1:1" (on at every VP start) and the loopback
	// harness's arm switch (off at every VP start). Neither is saved.
	constexpr const char* OneToOneStateCommand = "one_to_one_state";
	constexpr const char* OneToOneOff = "ColourSpace patches 1:1 is off.";
	constexpr const char* LoopArmStateCommand = "loop_arm_state";
	constexpr const char* LoopArmOff = "The loopback harness is not armed.";

	// For a state command: its refusal text for off; nullptr for any other
	// command.
	inline const char* OffTextFor(const std::string& command)
	{
		if (command == StateCommand) return Off;
		if (command == OneToOneStateCommand) return OneToOneOff;
		if (command == LoopArmStateCommand) return LoopArmOff;
		return nullptr;
	}
}
