# Teleport

Teleport is an experimental fork of [Deskflow](https://github.com/deskflow/deskflow). Its goal is to turn Deskflow into a software KVM for computers that share one or more monitors, keyboards, and mice.

Deskflow already moves keyboard and mouse control between computers. Teleport coordinates that handoff with the monitor input, so the complete workspace moves without a physical KVM switch.

## Intended experience

Teleport is intended to support macOS, Ubuntu, and Windows. Any supported system can act as the Deskflow server, and the other systems can connect as clients.

Each computer can use an HDMI or DisplayPort connection to a shared monitor. When Deskflow successfully switches to a computer, Teleport changes the monitor to the matching input. This works for hotkeys, screen edges, direction switching, computer cycling, screensaver switching, and returning to the server.

Monitor switching will use DDC only. Teleport will not depend on monitor vendor applications, infrared controls, USB control protocols, or simulated monitor-menu input.

## Monitor switching setup

The Deskflow server view has a separate `Monitor Switching` page. It does not add monitor-specific fields to the normal Deskflow server configuration.

The page discovers external monitors, reads computer names from the current Deskflow layout, identifies the server, and lets the user select participating computers. Teleport matches the monitor's EDID manufacturer, product ID, and model name against a bundled, versioned profile database. Entries use friendly names with the write value in brackets, such as `DisplayPort 2 [DDC 16]`.

When a profile matches, the user assigns an input to each computer and can enable switching immediately without sending test commands. Unknown monitors fall back to reported DDC capabilities or standard MCCS input values and retain the guided test and recovery workflow.

The setup page also provides `Import profile database...`. It validates a schema version 1 JSON file, saves it atomically, and merges its profiles over the bundled database. The uploaded database is stored at:

```text
~/Library/Application Support/Teleport/monitor-profiles.json
```

The complete format and import workflow are documented in [`docs/monitor-profile-database.md`](docs/monitor-profile-database.md).

The separate, versioned configuration is written atomically to:

```text
~/Library/Application Support/Teleport/monitor-switching.json
```

Editing a monitor or route disables runtime switching until the configuration is enabled again. A changed profile revision, renamed computer, or removed computer also invalidates the saved configuration.

## Design direction

The feature extends Deskflow without changing the behavior of its existing switching actions. A display input coordinator listens for each successful server screen-switch event and sends one DDC input command for the active computer. A DDC failure is logged without reversing Deskflow's keyboard and mouse handoff.

This keeps the existing Deskflow keyboard, mouse, networking, and screen-switching behavior intact. It also gives future features a clean extension point without adding monitor-specific logic to Deskflow's input code.

At startup, Teleport will select the DDC controller that matches the server operating system:

```text
Apple Silicon macOS -> Apple Silicon DDC controller
Intel macOS         -> unsupported controller
Ubuntu              -> unsupported controller
Windows             -> unsupported controller
```

The rest of Teleport uses a common display input controller interface and does not need to know which operating system is running. The Apple Silicon implementation uses IOKit and CoreDisplay for DDC/CI VCP `0x60` reads and writes. Linux, Windows, and Intel Mac placeholders keep those builds portable until their backends are implemented.

No Deskflow network protocol changes or special KVM hotkey actions are required.

Friendly input names and write values come from the matched monitor profile. Profiles can store separate write and read values for monitors such as the Samsung LC49G95T. Unknown monitors use the monitor's capability data when available and a standard fallback list otherwise.

The initial database contains 112 usable input-source profiles imported from `ddccontrol-db` plus a measured Samsung LC49G95T override from `monitor-switch`. The generated JSON records its source commits and licenses. `tools/import-monitor-profiles.py` rebuilds the bundled database from a pinned `ddccontrol-db` checkout.

DDC support also depends on the monitor, cable, dock, and adapter. Teleport should report these limitations clearly and leave Deskflow's normal screen switching available when changing the video input fails.

## Project status

The first integrated implementation is available for an Apple Silicon Mac running the Deskflow server and an unmodified Deskflow client on another computer. Version one supports one shared monitor.

The earlier standalone proof of concept is archived in [teleport-legacy](https://github.com/okonnu/teleport-legacy). It demonstrated direct DDC monitor input switching on macOS and will be used as a reference for the first platform implementation.

## Upstream and license

Deskflow is a free and open source keyboard and mouse sharing application. General Deskflow documentation is available in [`docs/Readme.md`](docs/Readme.md), and the upstream project is at [deskflow/deskflow](https://github.com/deskflow/deskflow).

This fork retains Deskflow's existing licenses, including GPL-2.0-only with its OpenSSL exception for the core application. The Apple Silicon DDC implementation retains the MIT attribution for [AppleSiliconDDC](https://github.com/waydabber/AppleSiliconDDC).

The bundled monitor profiles include GPL-2.0-only data from [ddccontrol-db](https://github.com/ddccontrol/ddccontrol-db) and an MIT-licensed profile from [monitor-switch](https://github.com/DimpiM/monitor-switch).
