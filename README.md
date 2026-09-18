# Hearth

Hearth turns a ZECTRIX NOTE4 into a shared household memory board. Speak at the
fridge, and the e-paper shows the resulting notes, shopping list, meal plan,
weather, and alarms. A dedicated Hermes agent reads the current board and uses
Hearth tools to file each request; the hub keeps the board and sends updates to
the device.

![Today screen with notes and Buy/Menu preview](docs/img/01-today.png)

Screens from the current demo board, rendered by the host simulator using the
firmware's canvas and screen code.

- **Today:** a prominent RTC-backed clock, local date and weather, important
  notes, the next alarm, and compact Buy/Menu previews.
- **Buy and Notes:** shopping, chores, packing, and reminders with owners,
  check marks, and voice or button deletion.
- **Menu:** breakfast, lunch, and dinner under each weekday.
- **Alarms and timers:** say “alarm at 7 for school” or “30 second timer.” Both
  ring once on the NOTE4 and disappear after firing.
- **Responsive recording:** hold the front button to speak. Upload and filing
  happen in the background, so pages remain usable and more requests can queue.

| Buy | Menu |
|---|---|
| ![Demo shopping list with checked items](docs/img/02-buy.png) | ![Demo weekday meal plan](docs/img/03-menu.png) |

| Notes | Pulse |
|---|---|
| ![Demo household notes with owners and check marks](docs/img/04-notes.png) | ![Device connection, battery, and last-heard status](docs/img/05-pulse.png) |

Hold the front button to record; after release, the request files in the
background and the board updates:

| Listening | Filing | Updated |
|---|---|---|
| ![Listening status on Today](docs/img/06-listen.png) | ![Filing status on Today](docs/img/07-filing.png) | ![Buy list after voice removal](docs/img/08-removed.png) |

See the [developer reel](promo/hearth-developer-reel.mp4) and its
[voiceover transcript](promo/reel-transcript.md) for an animated walkthrough.

## Flash and use

You need a **NOTE4 Developer Kit** (ESP32-S3 N16R8, not NOTE4C), ESP-IDF v6.1,
Hermes on the hub machine or an SSH host, and a reachable speech-to-text service.
The hub and NOTE4 must share a network.

1. Copy `.env.example` to `.env` and set the STT URL, hub LAN URL, and weather
   coordinates. Choose `HEARTH_HERMES_METHOD=local` or `ssh`; for SSH, set
   `HEARTH_HERMES_SSH_HOST`. The profile directory and command used to select
   the profile are also configurable there. Copy
   `firmware/sdkconfig.defaults.local.example` to
   `firmware/sdkconfig.defaults.local`, then set Wi-Fi and the same hub LAN URL.
2. Preview, install the dedicated Hermes profile, and start the hub:

   ```bash
   ./tools/install_hearth_profile.sh --dry-run
   ./tools/install_hearth_profile.sh
   python3 -m hub --host 0.0.0.0 --port 8790
   ```

3. Connect the NOTE4 by USB and flash:

   ```bash
   ./tools/flash_hearth.sh clean /dev/ttyACM0
   ```

   `clean` adds no sample data. A fresh hub board starts empty; reflashing keeps
   your existing board. For a showcase with shopping, all note kinds, a full
   week of meals, and an alarm, use `./tools/flash_hearth.sh demo /dev/ttyACM0`.
   Demo replaces the current hub board and first saves it under `backups/`.
   Weather comes from the hub's configured location.

Short-click the **front button** to cycle Today, Buy, Menu, Notes, and Pulse.
Click **up/down** to move through items; hold **up** to check or uncheck the
selected item, or hold **down** to delete it. Hold the **front button** (about
half a second) while speaking, then release to send the recording.
Idle standby, the wake-up budget, and the poster poll schedule live in
[battery and power](docs/POWER.md).

Wi-Fi and hub settings can also be changed over the 115200-baud USB console
with `hearth-set` and `hearth-save`; see [device setup](docs/VOICE.md).
For factory image backup or restore, see [factory flash guide](docs/FACTORY_FLASH.md).

MIT licensed; see [LICENSE](LICENSE).
