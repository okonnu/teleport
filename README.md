# BetterDisplay Deskflow KVM

Use one keyboard shortcut to switch a monitor, keyboard, and mouse between a Mac and a Linux computer.

This setup was tested with:

- Samsung Odyssey G9 LC49G95T
- Mac on HDMI
- Ubuntu on DisplayPort 2
- BetterDisplay on the Mac
- Deskflow with the Mac as server and Ubuntu as client

Shortcuts:

- Control-Option-U: Ubuntu and DisplayPort 2
- Control-Option-M: Mac and HDMI

## Install

Install BetterDisplay and Deskflow first:

```bash
brew install --cask betterdisplay
brew install deskflow
```

Edit the constants at the top of `BetterDisplayHotkeys.swift` if your monitor or input codes differ.

Run:

```bash
chmod +x install.sh
./install.sh
```

When System Settings opens, add this file to Input Monitoring:

```text
~/Library/Application Support/BetterDisplayHotkeys/BetterDisplayHotkeys
```

Copy `deskflow-server.example.conf` to `~/Library/Deskflow/deskflow-server.conf`. Replace `Ubuntu-PC` and `MacBook.local` with the names shown by Deskflow.

Keep the keyboard and mouse connected to the Mac. Run the Mac as the Deskflow server and Ubuntu as the client.

## Samsung G9 input values

The original LC49G95T uses these values:

- HDMI: 1
- DisplayPort 1: 15
- DisplayPort 2: 16

Other monitors may use different values.

## Privacy

The helper needs macOS Input Monitoring because it observes the same physical shortcut that Deskflow receives. The source only checks Control-Option-M and Control-Option-U. It does not save typed text or contain networking code.

Review the source and compile it locally with `install.sh`.

## License

MIT
