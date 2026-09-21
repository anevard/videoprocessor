# 2. Queue

![Queue configuration screen](images/02-queue.png)

Queue is an ordered profile editor. It controls how many frames VP keeps available and how it recovers after a render restart or an unusually deep queue.

## Profile controls

Use the [shared profile controls](README.md#profiles-order-inheritance-and-rules) to create profiles for different capture or timing conditions. The queue profile may also be selected by shortcut or rule.

## Buffering policy

- **Buffering policy** — Applies a complete baseline such as a balanced or low-latency queue policy. Choose **Custom** when you want to tune individual values.
- **Queue depth** — Maximum nominal number of frames held by the queue.
- **Lead frames** — Frames kept ahead of the currently presented position. More lead can absorb timing variation; less lead reduces latency.
- **Startup pre-roll** — Frames accumulated before normal presentation begins.
- **Target frames** — The desired steady-state queue target.
- **Active-picture lookahead** — Extra frames inspected ahead of presentation for active-picture decisions.
- **Queue reset delay** — Delay, in seconds, before VP resets/rebuilds the queue after a renderer restart.
- **Queue recovery threshold** — Percentage threshold that determines when an oversized queue should be recovered. Values above 100% are valid because the full queue path can include work outside the nominal capacity.

Editing an advanced queue value changes the profile to **Custom**. Start from a policy and tune only the values you have a measured reason to change.

---

[← Configuration guide index](README.md)
