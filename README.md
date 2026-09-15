# Teleport

Teleport is an experimental fork of [Deskflow](https://github.com/deskflow/deskflow). Its goal is to turn Deskflow into a software KVM for computers that share one or more monitors, keyboards, and mice.

Deskflow already moves keyboard and mouse control between computers. Teleport coordinates that handoff with the monitor input, so the complete workspace moves without a physical KVM switch.

## Intended experience

Teleport is intended to support macOS, Ubuntu, and Windows. Any supported system can act as the Deskflow server, and the other systems can connect as clients.

Each computer can use an HDMI or DisplayPort connection to a shared monitor. When Deskflow successfully switches to a computer, Teleport changes the monitor to the matching input. This works for hotkeys, screen edges, direction switching, computer cycling, screensaver switching, and returning to the server.

Monitor switching will use DDC only. Teleport will not depend on monitor vendor applications, infrared controls, USB control protocols, or simulated monitor-menu input.

## Monitor switching setup

The Deskflow server view has a separate `Monitor Switching` page. It does not add monitor-specific fields to the normal Deskflow server configuration.

The page discovers external monitors, reads computer names from the current Deskflow layout, identifies the server, and lets the user select participating computers. Users provide only friendly input labels. Raw DDC values are never requested or shown.

`Detect inputs and enable` reads the current server input as the recovery point, probes standard monitor inputs, and asks which selected computer appeared. Each probe stays visible for five seconds and restores the server input in a cleanup step. DDC readback verifies that the monitor accepted the probe. A contradictory readback is rejected, and a failed restore stops the sequence and leaves the feature disabled.

The separate, versioned configuration is written atomically to:

```text
~/Library/Application Support/Teleport/monitor-switching.json
```

Editing a monitor or route clears verification and disables runtime switching until all tests pass again. Renamed and removed Deskflow computers also invalidate the saved routes.

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

The friendly input names map internally to DDC VCP input source values discovered by the setup tests. Those values vary between monitor models, so Teleport stores the detected mapping without exposing raw values to the user.

DDC support also depends on the monitor, cable, dock, and adapter. Teleport should report these limitations clearly and leave Deskflow's normal screen switching available when changing the video input fails.

## Project status

The first integrated implementation is available for an Apple Silicon Mac running the Deskflow server and an unmodified Deskflow client on another computer. Version one supports one shared monitor.

The earlier standalone proof of concept is archived in [teleport-legacy](https://github.com/okonnu/teleport-legacy). It demonstrated direct DDC monitor input switching on macOS and will be used as a reference for the first platform implementation.

## Upstream and license

Deskflow is a free and open source keyboard and mouse sharing application. General Deskflow documentation is available in [`docs/Readme.md`](docs/Readme.md), and the upstream project is at [deskflow/deskflow](https://github.com/deskflow/deskflow).

This fork retains Deskflow's existing licenses, including GPL-2.0-only with its OpenSSL exception for the core application. The Apple Silicon DDC implementation retains the MIT attribution for [AppleSiliconDDC](https://github.com/waydabber/AppleSiliconDDC).
