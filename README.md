# Teleport

Teleport is an experimental fork of [Deskflow](https://github.com/deskflow/deskflow). Its goal is to turn Deskflow into a software KVM for computers that share one or more monitors, keyboards, and mice.

Deskflow already moves keyboard and mouse control between computers. Teleport intends to coordinate that handoff with the monitor input, so one shortcut can move the complete workspace without a physical KVM switch.

## Intended experience

Teleport is intended to support macOS, Ubuntu, and Windows. Any supported system can act as the Deskflow server, and the other systems can connect as clients.

Each computer can use an HDMI or DisplayPort connection to a shared monitor. A shortcut should switch Deskflow control to the selected computer and change the monitor to the matching input. Another shortcut should bring both control and video back.

Monitor switching will use DDC only. Teleport will not depend on monitor vendor applications, infrared controls, USB control protocols, or simulated monitor-menu input.

## Planned additions

- A Deskflow action for changing a monitor input with DDC.
- The ability to combine that action with Deskflow's existing `switchToScreen` action.
- Configurable monitors, HDMI and DisplayPort input values, and shortcuts.
- Separate DDC implementations for macOS, Ubuntu, and Windows.
- Automatic selection of the correct DDC implementation for the server operating system.
- Support for different monitor models through discovery and configuration instead of vendor-specific behavior.
- Clear failure handling when a monitor or platform does not support DDC.

## Design direction

The feature should extend Deskflow instead of changing the behavior of its existing actions. The proposed design introduces a small action registration interface, a common DDC controller interface, and one implementation for each supported server operating system.

This keeps the existing Deskflow keyboard, mouse, networking, and screen-switching behavior intact. It also gives future features a clean extension point without adding monitor-specific logic to Deskflow's input code.

At startup, Teleport will select the DDC controller that matches the server operating system:

```text
macOS   -> macOS DDC controller
Ubuntu  -> Linux DDC controller
Windows -> Windows DDC controller
```

The rest of Teleport will use the common controller interface and will not need to know which operating system is running.

No new network protocol is expected for the first version. The server can run both actions when a hotkey is pressed:

```text
Shortcut A -> switchToScreen("computer-a") + setDisplayInput("monitor", "HDMI 1")
Shortcut B -> switchToScreen("computer-b") + setDisplayInput("monitor", "DisplayPort 2")
```

The friendly input names will map to the DDC VCP input source values reported or required by the selected monitor. Those values vary between monitor models, so Teleport must allow manual configuration when discovery is incomplete.

DDC support also depends on the monitor, cable, dock, and adapter. Teleport should report these limitations clearly and leave Deskflow's normal screen switching available when changing the video input fails.

## Project status

Teleport currently starts from the upstream Deskflow codebase. The integrated DDC action described here is planned and is not yet part of this fork.

The earlier standalone proof of concept is archived in [teleport-legacy](https://github.com/okonnu/teleport-legacy). It demonstrated direct DDC monitor input switching on macOS and will be used as a reference for the first platform implementation.

## Upstream and license

Deskflow is a free and open source keyboard and mouse sharing application. General Deskflow documentation is available in [`docs/Readme.md`](docs/Readme.md), and the upstream project is at [deskflow/deskflow](https://github.com/deskflow/deskflow).

This fork retains Deskflow's existing licenses, including GPL-2.0-only with its OpenSSL exception for the core application. Any third-party DDC code added later must retain its original notices and be compatible with those terms.
