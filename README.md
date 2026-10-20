# zmod_esp32-nfc

A Wi-Fi NFC/RFID filament tag reader for the **Flashforge AD5X** and
**Flashforge Creator 5**, built from an **ESP32** and a **PN532** module.
Hold a spool's tag to the reader and the printer's slot gets the spool's
material and colour. One reader can serve several printers, running stock
firmware or [Zmod](https://github.com/ghzserg/zmod), and needs no USB port on
the printer. Everything is set up on the reader's own web page: Wi-Fi,
printers and their IP addresses, and how each printer is reached.

```
           ┌────────── ESP32 + PN532 ──────────┐
 tag ────► │ read the whole tag (NTAG, MIFARE   │
           │ Classic with Bambu/Creality/       │
           │ Snapmaker keys, Type 4)            │
           └──────┬──────────────┬──────────┬───┘
                  │              │          │
      Zmod + zmod_nfc      Zmod basic     Stock firmware
      Moonraker ──► plugin Moonraker      Flashforge local API
      decode, Spoolman,    CHANGE_ZCOLOR  msConfig_cmd
      slot popup on the    + lane_data    (material + colour)
      printer, lane_data
```

> **Status: early release.** Tested on an **ESP32-C3 SuperMini** with a
> **PN532 over I2C** and a **Creator 5** running Zmod and zmod_nfc: an
> OpenSpool NTAG read, sent through Moonraker, and assigned both from the
> printer's prompt and with the reader fixed to one toolhead. The firmware
> builds for ESP32, ESP32-S3 and ESP32-C3 without warnings. Its decoders and
> MIFARE key derivation match [zmod_nfc](https://github.com/DIEHARDave/zmod_nfc)'s
> (identical results for every tag in zmod_nfc's decoder tests, identical
> Bambu, Creality and Snapmaker keys). Not yet tried: MIFARE Classic tags
> (Bambu, Creality, Snapmaker) on the PN532, HSU wiring, the AD5X, and the
> **Zmod basic** and **stock firmware** methods. Reports are welcome.

## Printer methods

Each printer on the reader's list uses one of three methods:

| Method | Printer runs | What a scan does |
|--------|--------------|------------------|
| **Zmod + zmod_nfc** | Zmod with the [zmod_nfc](https://github.com/DIEHARDave/zmod_nfc) plugin | The **whole tag read** goes through Moonraker to the plugin (no extra port or service on the printer), which handles it exactly like a tag on a USB reader plugged into the printer: it decodes every format zmod_nfc knows, looks the spool up in **Spoolman** (spool ID on the tag, or the tag's UID), shows its **slot popup on the printer's screen** (or takes the slot you insert filament into), sets the slot through zmod_afclite's `SET_SPOOL_ID` or Zmod's `CHANGE_ZCOLOR`, and fills in Moonraker's **lane_data** for OrcaSlicer. The reader's web page can pick the slot too. |
| **Zmod basic** | Zmod, no plugin | Material and colour go to the slot through Moonraker (`CHANGE_ZCOLOR`), snapped to Zmod's material list and colour palette, and the slot's **lane_data** entry gets the material, colour, vendor, temperatures and the tag's Spoolman spool ID if it carries one. No Spoolman lookup. |
| **Stock firmware** | Flashforge firmware | Material and colour through the printer's local API (`msConfig_cmd` on port 8898) - that is all the stock API offers. Needs the printer's **serial number** and **check code**. On the Creator 5, the colour is snapped to its 24-colour palette and the material to its list, as its firmware shows nothing else. |

With Zmod basic and stock, pick the slot on the reader's web page after a
scan. With any method, a reader can also be fixed to one slot; see
[One reader per slot](#one-reader-per-slot).

## Hardware

* An **ESP32** board: classic ESP32 (DevKit, WROOM-32), ESP32-S3 or
  ESP32-C3 (SuperMini works).
* A **PN532** module, e.g. the common red or blue "PN532 NFC RFID V3" boards
  or Elechouse's. The PN532 reads ISO 14443A tags: NTAG21x, MIFARE
  Ultralight, MIFARE Classic and Type 4. It cannot read ISO 15693 tags
  (OpenPrintTag); zmod_nfc with an ACR1552U can.
* ESP32-C3 SuperMini boards have a weak antenna. If the reader drops off
  your network, change **Settings > Wi-Fi transmit power**: the usual advice
  of 8.5 dBm lost most packets on the tested board, which worked well from
  11 dBm up (15 dBm is the C3 default).
* Optional: an LED and an **active** buzzer. The board's own LED is used by
  default (GPIO 2 on ESP32 DevKits, GPIO 8 on the C3 SuperMini).

### Wiring

Power the PN532 from **3.3 V**, so its signals stay at the ESP32's 3.3 V
levels. Set the PN532's mode switches for the bus you use:

| Mode | Switch 1 (SEL0) | Switch 2 (SEL1) |
|------|-----------------|-----------------|
| I2C (default) | ON | OFF |
| HSU (UART) | OFF | OFF |

Default pins (all can be changed on the web page):

| Board | I2C SDA | I2C SCL | UART RX (to PN532 TXD) | UART TX (to PN532 RXD) | LED |
|-------|---------|---------|------------------------|------------------------|-----|
| ESP32 | 21 | 22 | 16 | 17 | 2 |
| ESP32-S3 | 8 | 9 | 18 | 17 | none |
| ESP32-C3 | 6 | 7 | 20 | 21 | 8 (lit when low) |

```
PN532          ESP32 (I2C)          PN532          ESP32 (HSU)
 VCC  ──────── 3V3                   VCC  ──────── 3V3
 GND  ──────── GND                   GND  ──────── GND
 SDA  ──────── GPIO 21               TXD  ──────── GPIO 16 (RX)
 SCL  ──────── GPIO 22               RXD  ──────── GPIO 17 (TX)
```

## Install

Build and flash with [PlatformIO](https://platformio.org/) (the VS Code
extension, or `pip install platformio`):

```sh
pio run -e esp32dev -t upload      # classic ESP32
pio run -e esp32-s3 -t upload      # ESP32-S3
pio run -e esp32-c3 -t upload      # ESP32-C3
pio device monitor                 # log output
```

Later updates can be uploaded from the web page (**Settings > Firmware**,
the file is `.pio/build/<board>/firmware.bin`).

## Setup

1. On first start the reader opens a Wi-Fi network **`zmod-nfc-setup-xxxx`**
   (password **`zmodnfc1`**). Join it; the setup page opens (or browse to
   `http://192.168.4.1`). The LED blinks slowly while the setup network is
   up.
2. Under **Settings**, enter your Wi-Fi network and password and save. The
   reader restarts and joins it. If it cannot connect, the setup network
   comes back, and the reader tries your network again every two minutes
   while nobody is connected to the setup network.
3. Open the reader at **`http://zmod-nfc-xxxx.local`** (the name is on the
   Settings tab) or its IP address from your router.
4. Under **Printers**, add each printer: name, IP address, AD5X or
   Creator 5, and the method. **Test** checks the connection.
   * **Zmod + zmod_nfc**: install or update zmod_nfc on the printer. The
     reader reaches it through Moonraker (port 7125); zmod_nfc adds
     endpoints to Moonraker's API rather than opening a port of its own.
   * **Zmod basic**: nothing to install; uses Moonraker on port 7125.
   * Both Zmod methods work when the reader's address is in Moonraker's
     `[authorization] trusted_clients`, which Zmod sets to every address by
     default. If you narrowed it, enter a Moonraker API key.
   * **Stock firmware**: enter the printer's serial number and its check
     code, the code the printer shows for LAN connections (the one
     OrcaSlicer and FlashPrint ask for when you add the printer).
5. On the **Scan** tab, choose which printer scans go to, and hold a tag to
   the reader.

You can also set a password for the web page (user `admin`) under
**Settings**.

## Using it

The **Scan** tab shows the last tag: its colour, what it decoded to and what
the printer did, plus the target printer's slots.

* **Zmod + zmod_nfc:** the printer's screen asks which slot the spool goes
  in, just like with a USB reader. Tap a slot there, insert the filament
  into an empty slot, or click a slot on the reader's page. **Send to
  printer again** reopens the popup.
* **Zmod basic / stock:** click the slot on the page.

### One reader per slot

Each printer entry has a **Slot for this reader** setting. Set it, and every
scan on this reader goes straight into that slot (or toolhead on the
Creator 5) with no question asked. Mount one reader at each IFS slot, set
each to its own slot, and a spool is assigned just by setting it on its
slot's reader.

With zmod_nfc, everything else still happens: the Spoolman lookup, lane
data, and Zmod's colour popup is skipped when you then insert the filament.
A reader can have a fixed slot on one printer and ask on another.

The LED (and buzzer) signal: a short blink when a tag is read, a long one
when the printer took it, three short ones on an error.

## Tag formats

| Format | Decoded on the reader | Decoded by zmod_nfc |
|--------|-----------------------|---------------------|
| OpenSpool (NDEF JSON) | yes | yes |
| Spoolman links and nfc2klipper `SPOOL:` | spool ID only | yes, with the Spoolman lookup |
| Bambu Lab (MIFARE Classic) | yes | yes |
| Creality (MIFARE Classic) | yes | yes |
| Snapmaker U1 (MIFARE Classic) | yes | yes |
| SpoolEase, Anycubic, Elegoo, Qidi | yes | yes |
| Simple text (`PLA:FF0000`, JSON) | yes | yes |
| TigerTag, OpenTag3D | no | yes |
| OpenPrintTag (ISO 15693) | the PN532 cannot read these | yes, with an ACR1552U or PN5180 on the printer |

The reader derives the Bambu, Creality and Snapmaker MIFARE keys from each
tag's UID, as zmod_nfc does, so those tags are read completely. With the
zmod_nfc method the raw read is decoded on the printer, so every format
zmod_nfc supports works, including ones the reader does not decode itself.

## Notes

* The reader and the printers talk plain HTTP on your local network. Do not
  expose the reader or the printers to the internet. Set a page password if
  others share your network.
* A tag that only names a Spoolman spool (a Spoolman QR/NFC link) has no
  material or colour of its own: use the zmod_nfc method, which looks it up.
* The reader sends a tag once; take it away and back to send it again, or
  press **Read the tag again**.

## Web API

The page uses a small JSON API, also usable from scripts: `GET /api/status`
(reader, Wi-Fi, last scan), `GET/POST /api/config`, `POST /api/active
{"printer": n}`, `GET /api/slots?printer=n`, `POST /api/assign
{"printer": n, "slot": s}`, `POST /api/resend`, `POST /api/cancel`,
`POST /api/test`, `POST /api/rescan`, `GET /api/networks`,
`POST /api/restart` and `POST /update` (firmware). The protocol between the
reader and zmod_nfc is in zmod_nfc's
[docs/remote-api.md](https://github.com/DIEHARDave/zmod_nfc/blob/main/docs/remote-api.md).

## Credits

The tag reading, decoders and key derivations are ported from
[zmod_nfc](https://github.com/DIEHARDave/zmod_nfc), which adapted them from
[OpenRFID](https://github.com/suchmememanyskill/OpenRFID) (GPL-3.0), building
on the Bambu Research Group RFID guide, the Creality CFS RFID research,
DnG-Crafts' ACE-RFID and U1-RFID, and the TigerTag project. The stock
Flashforge API details come from GhostTypes'
[ff-5mp-api](https://github.com/GhostTypes/ff-5mp-api-ts) documentation.

## License

GPL-3.0, see [LICENSE](LICENSE).
