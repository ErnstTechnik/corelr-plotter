# Design Requirements — ISR Dispatch

Module: **IsrDispatch** — sole owner of the periodic hardware timer and
its interrupt vector. Configures the timer and dispatches each tick to
the modules that need one; knows nothing about what they do with it.

Realizes: [`REQ-CoreLR-F-Actuation-0001`](../functional/REQ-CoreLR-F-Actuation.md)
(the actuation refresh rate this timer provides),
[`REQ-CoreLR-F-UserInterface-0009`](../functional/REQ-CoreLR-F-UserInterface.md)
(the time base the UI refresh limit needs)

Origin: maintainer proposal, 2026-09-09 — informed by the maintainer's own
ATTiny85 scheduler (`~/Arduino/ATTiny/Uart_RTx_USI_SCHED_11`,
`tnx5BasicSched.*`), reviewed when the module was designed.
Status: Implemented (2026-09-10)

## REQ-CoreLR-D-IsrDispatch-0001 — Single owner of the timer and its vector
<a id="REQ-CoreLR-D-IsrDispatch-0001"></a>

> **Amended 2026-10-06, implemented:** the rule becomes *one owner per timer*. `Stepper` owns **Timer1**, which is compare-driven and aperiodic and has no periodic tick to dispatch (`REQ-CoreLR-D-Stepper-0011`); `IsrDispatch` owns **Timer2** and its vectors. The original rationale — no module becomes the firmware's clock provider by accident — still holds: Timer1's ISR serves no one but the stepper.
Exactly one module in the firmware shall configure the periodic timer
and define its interrupt vector. No other module may touch the timer's
registers or declare that vector.

**Rationale (confirmed 2026-09-09):** the timer/ISR was previously owned
by `Stepper`, which made it *also* the firmware's clock
provider — so any other module needing a time base had to depend on
`Stepper` for something unrelated to stepper motion. Moving the
vector into a module of its own inverts that dependency: `Stepper`
becomes an ordinary tick *consumer*, exactly like every other one, and
adding a third consumer requires touching neither of the first two.
Concentrating the vector name (`TIMER2_COMPA_vect`) in one place also
means a future move to a different timer changes exactly one file.

## REQ-CoreLR-D-IsrDispatch-0002 — Dispatch order and content
<a id="REQ-CoreLR-D-IsrDispatch-0002"></a>

> **Amended 2026-10-06, implemented:** the consumers are now `SysTick_Tick()` and the servo frame (`PenControl`, `REQ-CoreLR-D-PenControl-0003`). Motion is no longer dispatched here, so "motion first" no longer applies — it is on its own, higher-value timer.
On each tick, the ISR shall do nothing but call each consumer's tick
entry point, in a fixed, documented order:

```
ISR(timer compare vect)
   |
   +--> Stepper_Tick()   /* motion: coil bits + hold countdown */
   |
   +--> SysTick_Tick()   /* time base: 10ms counter, see REQ-CoreLR-D-SysTick */
```

Motion is dispatched **first**: its deadline is the hardest in the
system (a late coil update is a physical stepper glitch), whereas the
time base only needs to be correct on average, and tolerates being
serviced a few microseconds later within the same tick.

The ISR itself shall contain no logic of its own — no counters, no
conditionals, no state. Anything a consumer needs belongs in that
consumer's `_Tick()`.

**Rationale:** keeps the ISR's worst-case duration equal to the sum of
its consumers' documented costs, with nothing hidden in the dispatcher
itself — the property that makes the 4kHz budget auditable at all.

## REQ-CoreLR-D-IsrDispatch-0003 — Tick frequency and compile-time validation
<a id="REQ-CoreLR-D-IsrDispatch-0003"></a>

> **Amended 2026-10-06, implemented:** Timer2 in CTC mode at **1 kHz** — prescaler 64, `OCR2A = 249`, `16 MHz / (64 × 250) = 1000` exactly; the three `#error` guards are unchanged. The ISR costs ~40 cycles per millisecond (0.25 % of the CPU, *estimate*). `SysTick` needs little precision (maintainer, 2026-10-06), so the shared 1 ms tick costs nothing in accuracy; the servo is what sets the 4 µs resolution, through compare-B.
The tick frequency shall be a software tunable
(`ISRDISPATCH_FREQ_HZ`, `SwConfig.h`), and the timer's prescaler and
compare value shall be **derived from it by macro**, not written by
hand. The derivation shall fail the build (`#error`) when the requested
frequency is not exactly reachable on the target clock:

- the clock assumption itself (`F_CPU`) is not what the derivation
  assumes;
- the requested frequency does not divide `F_CPU` exactly with the
  chosen prescaler (no silent rounding of the motion time base);
- the computed compare value does not fit the timer's counter width.

**Rationale:** carried over from the working `Stepper` Timer2
setup, which already had these three guards — they move with the timer,
they are not new. A silently-rounded stepper time base would show up
only as a slow drift in plotted dimensions, which is exactly the kind
of defect a compile-time check is worth having.

## Open items
- Whether a future third consumer (status-LED blink, periodic position
  save) justifies a full slot/callback scheduler in the style of the
  maintainer's ATTiny85 `tnx5BasicSched` — see
  [`REQ-CoreLR-D-SysTick.md`](REQ-CoreLR-D-SysTick.md)'s open items,
  where that trade-off is recorded.
