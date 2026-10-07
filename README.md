# Flip Clock Kit

A build-it-yourself flip clock. You put it together, it tells the time, that's the whole product — no WiFi, no app, no account, no cloud.

- **Mechanism:** [NotKuro's Flip Clock Mechanism](https://makerworld.com/en/models/908946-flip-clock-mechanism) (MakerWorld 908946)
- **Case:** [Tuny's case](https://makerworld.com/en/models/976040#profileId-948911) — print the **UPDATED SIZE** profile
- **Firmware:** rewritten standalone by Alex de Brantes (this repo). The stock firmware needs Home Assistant; this one needs nothing.

**What's in the box:** printed mechanism + flaps, printed case, wired electronics tray (ESP32 pre-flashed, stepper + driver, DS3231 RTC with battery, 5V supply, two time-set buttons on labeled leads), screws, bearings, this README, assembly PDF.

---

## Choose your own adventure

### Path A — "I just want a clock" (15 minutes)

1. Assemble the mechanism and case per the printed guide. Plug the labeled Dupont connectors where the labels say. No soldering — anything that needed solder is already done.
2. Plug in the 5V supply.
3. Set the time with the two buttons: **M+** advances a minute, **H+** advances an hour. Hold either to repeat.

Done. The DS3231's coin cell keeps time through power outages; on return it fast-forwards the drums to catch up. If the clock ever disagrees with your phone, the buttons are the whole interface.

### Path B — "I want to tinker"

The firmware is plain Arduino, one file, ~250 lines, no frameworks, no cloud. Open `firmware/flip_clock_kit/flip_clock_kit.ino` and it's all there.

**Things worth knowing before you touch it:**
- The stepper drives the **minute drum** directly. The hour drum advances mechanically once per minute-drum revolution — the firmware only counts minutes.
- `STEPS_PER_FLIP` (default 136.5) is the per-build calibration constant. Every printed mechanism is slightly different: if flips land short or overshoot, tune this number. The fractional accumulator handles non-integer values without drift.
- **Gear alignment matters more than the code.** When assembling, line the gears up so the hour flips exactly on the hour — if the hour gear sits slightly ahead, it'll flip early and the catch won't stop the flap from falling. Get this right mechanically; no firmware fixes it.
- Don't panic if a minute occasionally doesn't flip a flap and the next one flips two: that's print tolerance, not a bug. It averages out.
- Coils de-energize between flips — no heat, no holding current, no buzz.
- Position is saved to flash every 15 minutes and on every button press, so a power outage costs at most a short catch-up on boot.

**Ideas, easiest first:**
1. **Night silence** — skip flips between midnight and 6 AM (the RTC knows the time; the drums just wait).
2. **WiFi + NTP** — add WiFiManager and NTP sync, keep the DS3231 as fallback. The `advanceClock()` function is the seam: point NTP at it.
3. **Chime** — a piezo on a free GPIO, one beep on the hour.
4. **ESPHome / Home Assistant port** — the stock Tuny firmware went this way. You'd regain network time and automations, and lose the gift-friendly simplicity. Your call.

### Path B+ — "Point a coding agent at it"

If you'd rather describe what you want than write it: this repo is written to be agent-legible. Paste the block below into your coding agent along with your request.

> **Hardware map — flip clock kit**
> - ESP32 dev board (Arduino framework). I2C: SDA=GPIO21, SCL=GPIO22.
> - DS3231 RTC on I2C (battery-backed). Source of truth for time. RTClib.
> - 28BYJ-48 stepper via ULN2003 driver: IN1=GPIO16, IN2=GPIO17, IN3=GPIO5, IN4=GPIO18. Half-step drive, 8-step sequence, de-energized between moves.
> - Buttons (to GND, INPUT_PULLUP): HOUR=GPIO32, MIN=GPIO33. Short press steps once; hold repeats.
> - Built-in LED on GPIO2 blinks per flip. 5V 2A barrel-jack supply.
> - `firmware/flip_clock_kit/flip_clock_kit.ino` is the entire firmware (~250 lines).
> - Key functions: `flipMinute()` (one drum flip), `advanceClock(n)` (move RTC + drums n minutes), `stepMotor(steps, delayMs)` (raw drive with fractional-step drift correction).
> - Calibration constants at top of file: `STEPS_PER_FLIP`, `STEP_DELAY_MS`, `BOOT_STEP_DELAY_MS`, `MAX_BOOT_FLIPS`.
> - Assumption: hour drum is mechanically linked to the minute drum. If changing to independent hour drive, that assumption lives in `advanceClock()` and the boot fast-forward in `setup()`.
> - Constraints: keep it working with zero network dependency by default; any WiFi feature must degrade gracefully to the RTC.

---

## Attribution & license

- Flip mechanism by **NotKuro**, case by **Tuny** — printed under MakerWorld's Standard Digital File License for personal, non-commercial gifting, with credit. If you print your own: go boost their models.
- Firmware in this repo by **Alex de Brantes**, MIT licensed. Do what you want with it.
- Kit assembled as a Christmas 2026 gift. If you're reading this, it worked.
