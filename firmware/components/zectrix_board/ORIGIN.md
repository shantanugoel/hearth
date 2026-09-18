Vendored from https://github.com/itopinion/zectrix-note4-epd-demo
(`components/zectrix_board`, MIT, Copyright 2026 Zectrix Lab).

Local addition: `idf_component.yml` so `espressif/esp_codec_dev` is pulled
with the board rather than by the application.

Local changes for Hearth:

- `zectrix_button_logic.h` holds the press classifier (debounce, click vs long
  press) as plain millisecond arithmetic, and `ButtonTask()` is a thin sampler
  around it. Hold-to-speak is now timed from the press edge, not from the end
  of the debounce, at 450 ms instead of 220 ms. The rules are unit-tested on a
  host in `tests/test_hearth_model.cc`.
- `TickPower()` ticks charge detection with the last battery sample, so the ten
  ADC conversions behind `ReadPowerSnapshot()` no longer need to run 20 times a
  second.
- `ReleaseAudio()` closes the codec over I2C, stops the I2S channels, and drops
  the rail; `AudioCodec::Stop()` and a re-`Start()` in `PrepareAudio()` make a
  rail power cycle safe.
- `Init(Config)` defaults to `enable_nfc = false`: the tag front end and its
  field-detect task stay powered down because Hearth has no use for them yet.
