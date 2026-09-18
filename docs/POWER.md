# Power and battery

The NOTE4 is a fridge poster: it sits unchanged for hours, then someone speaks
to it for five seconds. Almost every milliamp should therefore be the standby
bill, not work. This file records what the firmware does about that, what it
costs, and what is left.

No numbers below are measured yet — they are what each change is *for*. To turn
them into a battery figure, measure (see "Measuring" at the end).

## What changed in v0.9.2

### 1. The loop sleeps instead of polling

The board loop used to wake every 50 ms and, on each wake, take ten battery ADC
samples, ask the Wi-Fi driver for the current RSSI, walk the poster state, and
check the alarm. That is roughly 200 ADC conversions and 20 driver round trips
per second, forever, with nothing to show for it — the CPU never got an idle
stretch long enough to matter.

Now one queue carries every wake-up source (a button gesture from a small relay
task, a finished recording, a finished HTTP call) and every deadline the loop
asked for. `RunDueWork()` runs what is due and returns the next deadline; the
loop blocks until one of them fires. The board's wake budget is now explicit:

| Work | Cadence |
|---|---|
| Charge detect tick (two GPIO reads) | 500 ms while USB power is present, 2 s on battery |
| Battery ADC (10 samples) | 30 s on battery, 10 s while charging, now on a button press |
| RSSI for the footer | 5 s |
| RTC read + clock repaint | 1 s on Today, 30 s on other pages |
| Alarm check | wakes on the due second; 60 s while an alarm is far off or absent |
| Poster fetch | see below |
| Ack expiry, snap-to-Today | one deadline each, armed once |

`ChargeStatus` holds its "power present" verdict for one second, so the cheap
charge tick has to keep running twice a second while the board is plugged in;
that is two GPIO reads, not an ADC burst.

### 2. A poster fetch no longer repaints the panel

The fetch that came back every 10 seconds *always* painted a full-screen
partial refresh, even when the poster was identical. So the panel ran its
charge pump six times a minute, 8640 times a day, to redraw the same pixels —
and any button press during one of those refreshes waited behind it.

Two fixes:

- A fetch that differs only in the `clock` field is applied and *not* painted.
- Every paint now compares the new frame with the frame on the glass and
  refreshes only the rows that differ. A minute tick is a handful of rows
  instead of 300; a voice status change is one row; an unchanged frame costs no
  panel command at all.

This is also the answer to "the page turn is laggy": an e-paper partial
refresh is a few hundred milliseconds of physical switching that cannot be
removed, but the page turn no longer queues behind a refresh the hub never asked
for.

### 3. Poster polling is adaptive

`firmware/main/hearth_schedule.h` owns the rule, unit-tested on the host:

| Situation | Next fetch |
|---|---|
| Recording, uploading, or the agent is still filing | 2 s |
| Somebody pressed a button in the last 2 minutes | 10 s |
| Idle | 60 s |
| Idle for 10 more fetches with nothing changed | 5 min |
| Last fetch failed (hub restarting, Wi-Fi down) | 20 s, and no back-off |

The deliberate cost: a note filed from another device appears up to a minute
later on a board nobody is touching, and weather lags by up to a minute. A
button press pulls the interval straight back to 10 s. Alarms are not affected:
the PCF8563 keeps the time and the loop schedules a wake on the alarm's due
second, so a "30 second timer" still rings in 30 seconds.

### 4. Wi-Fi sleeps between requests

`WIFI_PS_MAX_MODEM` with `listen_interval = 5` is the default: the radio wakes
about every five beacon intervals instead of every one. All Hearth traffic is
device-initiated, so inbound latency of a few hundred milliseconds is invisible
on a poster. The loop stays in `WIFI_PS_MIN_MODEM` while a recording, upload, or
board write is queued or in flight, and while a button has been pressed within
two minutes (plus a five second grace), and drops to deep sleep otherwise. An
idle fetch therefore waits a few hundred ms for its response instead of
switching power-save modes twice every poll — the first version of this toggled
`Set ps type` on every fetch, which cost more than it saved. A filing turn that
the hub never finishes is deliberately *not* a reason to stay awake.

### 5. NFC stays down

`ZectrixBoard::Init()` now takes a config and defaults to `enable_nfc = false`.
Before, `Init()` powered the tag front end, kept it powered, added a GPIO ISR,
and started a 3 KB field-detect task — for a feature PLAN.md puts out of scope
for v1. Nothing in Hearth reads tags.

### 6. Audio is torn down after use

Recording and the alarm chime now close the codec registers over I2C, switch the
speaker PA off, disable both I2S channels, and drop GPIO42; `PrepareAudio()`
re-opens the codec and re-enables I2S on the next use. Be realistic about the
size of this: GPIO42 is a *shared* peripheral rail and `I2cDevice` re-asserts it
before every RTC or NFC transaction (`BoardI2cForcePowerOn`), so the next clock
read powers it again within a second. Treat it as correct teardown and no
clocking into an unpowered codec, not as a standby saving.

## What is still on the bill

- **Standby Wi-Fi**: modem sleep is only as deep as the AP's beacon and DTIM
  interval. A router that uses DTIM 3 or shorter beacons costs the board more.
- **GPIO42** stays up because the RTC path insists on it (above).
- **The panel rail** stays up on purpose: Today renders a live clock, and the
  panel needs its rail to refresh. E-paper holds an image with no power at all,
  so if the clock freezing while the fridge is untouched is acceptable, powering
  the panel off after a few idle minutes and doing one full refresh on the next
  press is a real saving — measure the rail first (`zectrix_epd_power_off` then
  ammeter; the driver's BUSY pull-up should also come off with the rail, or it
  back-feeds the unpowered controller).
- **USB-Serial/JTAG console** (`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`) is kept so
  provisioning and logs work over `/dev/ttyACM0`. A shipping build that drops it
  buys whatever the USB block idles at; it also costs the `hearth-set` REPL.
- **160 MHz fixed core** (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_160`). Enabling
  `CONFIG_PM_ENABLE` with DFS, or dropping to 80 MHz, is worth a try: JSON and
  20 MHz SPI do not need 160 MHz, but Wi-Fi throughput gets worse at lower
  clocks, so measure the upload path too.

## The big one still standing: sleep between fetches

The loop is now event-driven, which is what makes real sleep possible. Deep
sleep between poster fetches (60 s, or until the next alarm) is the change that
turns days into weeks:

- Wake sources are available: `esp_sleep_enable_gpio_wakeup()` covers all three
  buttons (`GPIO_NUM_0/18/39`), and the PCF8563 alarm output on `GPIO_NUM_5` can
  wake the board for a chime instead of polling — the driver already exposes
  `IsAlarmFired()` and an interrupt hook.
- Costs to design around: each wake pays a Wi-Fi reconnect (about a second,
  ~50 mA·s), PSRAM is lost so clip buffers must be allocated fresh, and the
  panel must be powered, refreshed, and powered again.
- With 2000+ mAh, a board drawing 60 mA all day lasts about a day and a half;
  the same board averaging 8 mA lasts about ten days, and averaging 1 mA lasts
  months. The sleep redesign is what moves the second number, not the third.
- Doing this without hardware in the loop is not advisable: light and deep sleep
  interact with octal PSRAM, the USB console, and the panel's BUSY timing, and
  all three need a scope or a shunt on the fridge.

## Measuring

1. Power the board from a bench supply or a PPK2 in current-monitor mode
   through the battery pads, not USB-C (USB-C powers the board directly and
   hides the battery path).
2. Record 10 minutes with the board idle on Today, and note the average.
3. Press OK once, then hold OK for two seconds and speak, and read the peaks.
4. Repeat after each knob: `WIFI_PS_*`, `listen_interval`, `CONFIG_PM_ENABLE`,
   CPU frequency, panel rail off during idle.

`hearth-stats` on the USB console reports the idle-wake and refresh counters
above (`wakes`, `refreshes`, `paints skipped`, poster fetches versus repaints,
the current poll interval, and battery mV/%), so the effect of each knob can be
read off the board without a scope. The log line `RTC alarm set ...` confirms an
alarm was handed to the RTC rather than polled for.

Measured on this board over the serial console, powered from USB (2026-09-18):

```
before   46 wakes/s               (the old 50 ms loop with ten ADC reads)
after     3.2 wakes/s             (charge tick + minute gate + radio poll)
poster   13 fetches, 0 repaints   (was: a full repaint on every fetch)
radio    one "Set ps type" at the two minute mark, then quiet
```

Average current is still unmeasured — do that before moving the ladder, and note
`hearth-stats` for the window so it can be attributed to a ladder state.
