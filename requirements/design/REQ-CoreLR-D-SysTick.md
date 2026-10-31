# Design Requirements — SysTick

Module: **SysTick** — the firmware's coarse time base. Counts periodic
ticks dispatched by [`IsrDispatch`](REQ-CoreLR-D-IsrDispatch.md) and
exposes them as an 8-bit counter that any module can read without a
critical section.

Realizes: [`REQ-CoreLR-F-UserInterface-0009`](../functional/REQ-CoreLR-F-UserInterface.md)
(bounded display-refresh latency needs a time base)

Origin: maintainer proposal, 2026-09-09 — modelled on the maintainer's own
ATTiny85 scheduler (`~/Arduino/ATTiny/Uart_RTx_USI_SCHED_11`,
`tnx5BasicSched.*`), reviewed when the module was designed; see the open items
for what was deliberately *not* carried over.
Status: Implemented (2026-09-10)

## REQ-CoreLR-D-SysTick-0001 — 8-bit counter, atomically readable
<a id="REQ-CoreLR-D-SysTick-0001"></a>
The time base shall be a **single 8-bit** counter, incremented by
`SysTick_Tick()` (called from the ISR) and read by any module from
main-loop context via a getter.

An 8-bit width is a requirement, not an accident: a single-byte read is
inherently atomic on 8-bit AVR, so readers need **no** `cli()`/`sei()`
critical section. A wider counter would need one on every read — and the
readers here are on the busiest loop path in the firmware.

**Rationale:** this is the property the maintainer's ATTiny85 scheduler
documents explicitly on its own two getters — `tnx5sched_GetTmrLow()`
*"atomic access: no cli()/sei()"* versus the 16-bit
`tnx5sched_GetTmr()` *"uses cli()/sei() for atomic access"*. We keep
only the 8-bit case and thereby avoid the critical section entirely.

## REQ-CoreLR-D-SysTick-0002 — Tick period, derived and compile-time checked
<a id="REQ-CoreLR-D-SysTick-0002"></a>

> **Value change 2026-10-06, implemented:** with `ISRDISPATCH_FREQ_HZ = 1000`, `SYSTICK_PERIOD_MS = 10` gives a divider of exactly **10** (was 40). The derivation and its guards are unchanged. The time base's accuracy is not critical to the project.
The tick period shall be a software tunable in **milliseconds**
(`SYSTICK_PERIOD_MS`, `SwConfig.h`), and the number of ISR ticks per
SysTick increment shall be **derived from it by macro** against
`ISRDISPATCH_FREQ_HZ` — never written by hand.

The derivation shall fail the build (`#error`) when the requested period
is not reachable:
- it does not divide the ISR frequency **exactly** (no silently rounded
  time base);
- the resulting divider is 0 (period shorter than one ISR tick);
- the resulting divider does not fit `uint8_t`.

Value in use, verified against the 4kHz ISR:
`SYSTICK_PERIOD_MS = 10` → divider = exactly 40, no remainder.

## REQ-CoreLR-D-SysTick-0003 — Wraparound-safe comparison, bounded period
<a id="REQ-CoreLR-D-SysTick-0003"></a>
Elapsed-time tests shall use **unsigned subtraction**, which is correct
across counter wraparound:

```c
if ((uint8_t)(SysTick_Get() - lastTick) >= periodTicks) { ... }
```

For that to be unambiguous, every period compared this way shall be
**at most half the counter range minus one** — i.e. ≤ 127 ticks. A
`SYSTICK_TICKS_MAX` macro shall express this bound, and each period
constant shall be checked against it at compile time (`#error`).

At the proposed 10 ms period: the counter wraps every 2560 ms, and the
usable maximum period is 1270 ms. Verified 2026-09-09 — every existing
constant fits with room to spare:

| Constant | Value | Ticks | Within 127? |
|---|---|---|---|
| `LCDJOYSTICKUI_LONGPRESS_MS` | 800 ms | 80 | yes |
| `LCDJOYSTICKUI_REPEAT_MS` | 200 ms | 20 | yes |
| `LCDJOYSTICKUI_REFRESH_MS` | 50 ms | 5 | yes |

**Rationale:** taken directly from the maintainer's ATTiny85 scheduler, which
bounds its periods to `0x7E` (8-bit) / `0x7FFE` (16-bit) and enforces it
with a `#error` — the same half-range constraint, for the same reason.

## REQ-CoreLR-D-SysTick-0004 — Skip missed periods, do not catch up
<a id="REQ-CoreLR-D-SysTick-0004"></a>
A periodic action whose deadline was missed (because `loop()` was
blocked longer than its period) shall resume from **now**, not replay
the periods it missed:

```c
lastTick = SysTick_Get();      /* skip: correct for a display refresh */
/* NOT: lastTick += periodTicks;   catch-up: would fire repeatedly */
```

**Rationale (found 2026-09-09 while reviewing the ATTiny85 scheduler):**
that scheduler advances its slot timestamp by exactly one period per
pass (`pslot->t = t_old + p`), so after a long block a slot fires on
several *consecutive* passes until it catches up. Traced concretely: a
40-tick block with `p = 10` makes the slot fire 4 times in a row. For a
display refresh that is precisely the pile-up this whole mechanism
exists to prevent — four back-to-back 10 ms redraws.

Catch-up is the right semantics for a task that must not lose its time
base (periodic sampling, integration); skip is the right semantics for
an idempotent refresh, where only the latest state matters. **CoreLR's
only periodic action today is a display refresh**, so skip is the rule.
Should a catch-up consumer ever appear, it will state so explicitly in
its own requirement rather than changing this default.

## Open items
- **Not carried over from the ATTiny85 scheduler:** the slot/callback
  table (`TNX5SCHED_SLOT_NUM` slots, each with period/offset/enable/
  function pointer). Deliberate — only *one* CoreLR task is genuinely
  periodic (the LCD refresh). `Motion_Task()`,
  `SDReader_Task()` and `Stepper_Task()` are "do a bounded unit
  of work whenever possible", which must run every iteration, not on a
  period. A 5-slot table would cost ~34 bytes of RAM (at 84% used) plus
  indirect calls that prevent inlining, to schedule a single task.
  Revisit if further genuinely periodic tasks appear (status-LED blink,
  periodic position save to EEPROM) — at which point the maintainer's
  scheduler is the model to adopt, with `-0004`'s skip/catch-up choice
  made per slot.
- **Robustness note for whoever implements a critical section here or
  elsewhere:** the ATTiny85 scheduler's 16-bit paths use
  `cli(); ...; sei();`, which unconditionally *enables* interrupts on
  exit and therefore misbehaves if called from within an existing
  critical section. CoreLR's
  `Stepper_TakePendingPenAction()` already uses the robust form
  (`previousSreg = SREG; cli(); ...; SREG = previousSreg;`) — keep that
  form everywhere. (`SysTick` itself needs no critical section at all,
  per `-0001`.)
