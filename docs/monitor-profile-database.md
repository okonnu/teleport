# Monitor profile database

Teleport ships a versioned JSON database for DDC input switching. Open `Monitor Switching` and select `Import profile database...` to add or replace profiles without rebuilding the app.

An imported database is validated before it is saved to:

```text
~/Library/Application Support/Teleport/monitor-profiles.json
```

Imported profiles are merged over bundled profiles by profile ID or by manufacturer and product ID. Profiles that share a model name remain separate.

The setup page matches profiles using the normalized model name only. Capitalization, spaces, and punctuation are ignored, but partial and fuzzy matches are not used. Every matching profile is shown in the shared monitor list. When no profile matches, the list shows a Generic profile that uses DDC capability discovery and guided testing.

## Format

```json
{
  "schemaVersion": 1,
  "databaseVersion": "example-1",
  "profiles": [
    {
      "id": "SAM-7052",
      "manufacturerId": "SAM",
      "productId": 28754,
      "modelNames": ["LC49G95T"],
      "revision": 1,
      "source": "hardware test",
      "inputs": [
        {
          "id": "hdmi",
          "label": "HDMI",
          "writeValue": 17,
          "readValues": [1]
        },
        {
          "id": "displayport-2",
          "label": "DisplayPort 2",
          "writeValue": 16,
          "readValues": [4]
        }
      ]
    }
  ]
}
```

`writeValue` is the value sent to VCP `0x60`. `readValues` contains the values the monitor may return after that input is selected. They can differ.
The setup page shows both values when they are known, for example `HDMI [w 17, r 1]`.

Profile IDs, input IDs, and write values must be unique within their scope. DDC values must be integers from 0 through 65535. Increase `revision` whenever a profile's inputs or values change. A revision change disables an active configuration until the user reviews and enables it again.

Matched profiles are trusted and enabled without test commands. Unknown monitors continue through guided DDC testing.

## Rebuilding the bundled database

Use a checkout of `ddccontrol-db` at the commit recorded in `tools/import-monitor-profiles.py`:

```text
python3 tools/import-monitor-profiles.py /path/to/ddccontrol-db/db src/apps/res/monitor-profiles.json
```

The importer reads explicit VCP `0x60` controls first. It uses active capability-string values only when a monitor file has no explicit input list. Commented and malformed definitions are ignored.
