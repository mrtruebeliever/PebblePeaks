# Gemeenschappelijke conventies voor nieuwe Pebble-spellen

Dit document hoort bij de spelplannen in deze map (tumble.md, vuurtoren.md,
pebble-peaks.md, duif-koerier.md). Lees dit EERST — het bevat alle
omgevingskennis en valkuilen uit eerdere projecten. Referentie-implementatie
voor vrijwel alles: **`PebbleDiver/`** (werkend, gepubliceerd spel).

## Doelplatform

- Pebble Time 2, platform **emery**, 200×228 kleuren-LCD, knoppen BACK/UP/SELECT/DOWN,
  touchscreen, accelerometer, kompas, hartslag.
- Alleen `"targetPlatforms": ["emery"]` in package.json.
- Layout altijd via `layer_get_bounds()`, geen hardcoded 200/228 in logica.

## Nieuw project opzetten

```bash
export PATH="$HOME/.local/bin:$PATH"     # nodig in elke shell
cd ~/Development/pebble
pebble new-project <Naam>                # C-project
```

Daarna in package.json: `author: "mrtruebeliever"`, `license: "MIT"`,
`enableMultiJS: false`, alleen emery, `watchapp.watchface: false`,
`messageKeys` weghalen (deze spellen zijn offline, geen JS nodig).
Hernoem `src/c/<Naam>.c` naar `src/c/main.c`.

⚠️ **Verplicht:** ook zonder icoon/afbeeldingen altijd een lege
`"resources": { "media": [] }` in `package.json`'s `pebble`-object laten
staan. De nieuwe Core Devices/rePebble-telefoon-app parset `appinfo.json`
strikt en verwacht een `resources`-veld zonder default — ontbreekt het,
dan faalt sideloaden op de telefoon met de generieke melding "Error while
sideloading app" (geen duidelijke foutmelding, en build/emulator merken
er niets van). Na het toevoegen/wijzigen van deze sleutel altijd eerst
`pebble clean` vóór de volgende `pebble build`, anders pikt waf de
appinfo.json-wijziging niet op.

## Bouwen / draaien / testen

```bash
pebble build          # output filteren: | grep -viE 'SyntaxWarning|escape sequence|:param'
pebble install --emulator emery
pebble screenshot --emulator emery pad.png     # native 200x228 PNG, ~1s
```

- **NOOIT `pebble logs` draaien** — blokkeert voor eeuwig, ook onder `timeout`.
- **Emulator-onbetrouwbaarheid**: `pebble install`/`pebble screenshot` lopen
  af en toe vast met een `libpebble2.exceptions.TimeoutError`, soms na een
  al geslaagde install. Oorzaken tot nu toe gezien: een oud weesproces
  (`qemu-pebble`) dat een CPU-kern volledig bezet houdt, of de actieve
  emulator zelf die niet meer reageert (display-probleem — het
  emulatorvenster is dan niet meer zichtbaar). Herstel: `ps aux | grep -E
  "qemu|pypkjs"` om te zien welk paar (qemu + pypkjs) actief is/hoort te
  zijn volgens `/tmp/pb-emulator.json`, en bij twijfel **eerst aan de
  gebruiker vragen** voor je een proces killt (zeker het proces dat wél in
  `pb-emulator.json` staat, niet alleen een weesproces) — een `pebble
  install` daarna start vanzelf een nieuwe instantie. Blijf het niet
  blind herhalen; als een herstart niet werkt is het waarschijnlijk een
  probleem aan de gebruikerskant (display/grafische omgeving) dat je zelf
  niet kan oplossen — meld dat en vraag hoe verder.
- Knoppen headless: **`PebbleDiver/tools/press.py`** (kopieer naar het nieuwe
  project). Gebruik: `tools/press.py select sleep:400 hold:select:1500 back`.
  Het script praat via de **pypkjs-websocket** (poort uit `/tmp/pb-emulator.json`,
  veld `emery.*.pypkjs.port`). Nooit rechtstreeks naar de qemu-poort verbinden
  (die wordt door pypkjs bezet gehouden; extra verbindingen worden nooit gelezen).
  Interpreter: `~/.local/share/uv/tools/pebble-tool/bin/python` (heeft libpebble2).
- Kantelen (accel) headless: `pebble emu-accel custom --file <csv>` met regels
  `x,y,z` (milli-G, 1000 = 1 G), óf breid press.py uit met een `tilt:x:y:z`-
  commando dat een `QemuAccel`-packet stuurt via dezelfde websocket-relay
  (`WebSocketRelayQemu`, zie libpebble2 `transports/qemu/protocol.py`).
- In subagents: geen background-taken starten en daarop wachten (stallen);
  gebruik synchrone `&&`-ketens met `sleep`.

## ⚠️ Emulator-persist = het echte savegame van de gebruiker

De gebruiker speelt zelf in deze emulator. `persist_*`-data blijft staan over
installs heen. Bij het testen van endgame/cheats: overrides alleen in RAM
(in `load_progress` ná het lezen), `return;` tijdelijk bovenin
`save_progress()`, en alle TEMP-regels verwijderen vóór de eindbuild.
Nooit persist wissen of een cheat-build laten staan.

## Code-architectuur (volg PebbleDiver/src/c/main.c)

- Eén C-bestand. Eén fullscreen `Layer` met `update_proc` die op state switcht.
- State-machine: `ST_TITLE / ST_PLAYING / ST_PAUSED / ST_GAMEOVER / ST_SHOP`
  (+ evt. `ST_VICTORY`).
- Game loop: `AppTimer` van 33 ms (~30 fps), alleen actief in ST_PLAYING;
  hertimer bovenin de callback.
- Physics in **8.8 fixed point** (`<<8`), `sin_lookup`/`cos_lookup` met
  `TRIG_MAX_ANGLE`/`TRIG_MAX_RATIO` voor hoeken.
- Entities in een statische pool met `type == NONE` als vrij.
- Alles tekenen met primitives (fill_rect/circle, draw_line, GPath); geen
  PNG-resources behalve het menu-icoon.
- Knoppen: `window_raw_click_subscribe` voor vasthoud-knoppen (down/up-events),
  gewone `single_click` voor navigatie. Let op: raw en single niet mengen op
  dezelfde knop.
- **Game-over input-lock**: 1 s knoppen negeren + hints pas daarna tonen
  (voorkomt per-ongeluk-herstart door een nog ingedrukte knop).
- **Auto-pauze**: `app_focus_service_subscribe`; bij focusverlies tijdens
  ST_PLAYING → pauzeren en timer stoppen.
- Vibes spaarzaam: korte puls bij schade, lange bij dood, dubbele bij fout/win.

## Wobble + magneet-les (uit PebbleDiver)

Entities die hun y elke tick uit `base_y + sin(...)` herberekenen: elke
externe verplaatsing (magneet, duw) moet `base_y` aanpassen, niet alleen `y`.

## i18n (verplicht: EN fallback, NL, FR, DE, ES)

Volg exact het patroon in PebbleDiver: `TR[NUM_LANGS][NUM_STRS]`-tabel,
`pick_language()` met `i18n_get_system_locale()` + `strncmp` op 2 letters.
Valkuilen:
- Pebble-systeemfonts dekken alleen **Latin-1**; het ●-teken bestaat NIET →
  teken zulke symbolen zelf met een cirkeltje.
- Duits/Frans/Spaans zijn lang: check tekstbreedtes (Gothic 14 ≈ 5,5 px/teken,
  18 bold ≈ 9, 24 bold ≈ 11–12, 28 bold ≈ 14). Test visueel door de taal
  tijdelijk te forceren (`s_tr = TR[L_DE];` bovenin pick_language, daarna
  weghalen) en screenshots te maken. Umlauten/ß/¡/é renderen prima.

## Kleuren

64-kleuren GColor-palet. Beproefde combinaties: water/lucht-dieptebanden
(VividCerulean → BlueMoon → CobaltBlue → Blue → DukeBlue → OxfordBlue),
HUD zwart met witte tekst, accenten ChromeYellow/Orange/ShockingPink/
SpringBud/Magenta/Celeste. "Magisch glinsteren": kleur cyclen per 4 ticks
door {Magenta, Cyan, Yellow, SpringBud} + knipperende witte ring.

## Release & store (pas na goedkeuring van de gebruiker)

Conventies zoals PebbleDiver/PebbleKimai:
- `CHANGELOG.md` (## versie + strakke bullets), MIT `LICENSE` op naam
  **mrtruebeliever**.
- `store/`: DESCRIPTION.md (Tagline / Short ≤140 / Full + FEATURES),
  icon-80.png, icon-144.png, banner-720x320.png, screenshots/ (genummerd,
  native 200×228), demo-GIF 200×228 (Pillow gebruiken — ImageMagick/ffmpeg
  ontbreken op dit systeem).
- `tools/`: gen_icon.py (menu-icoon 25×25 + `menuIcon: true` in package.json),
  gen_marketing.py, release.py (project-agnostisch, kopieer uit PebbleDiver)
  → `build/<Naam>-release.pbw`.
- Git: LOKALE config `user.name "mrTrueBeliever"`,
  `user.email "11655167+mrtruebeliever@users.noreply.github.com"`. Vóór commit
  alles scannen op de lokale gebruikersnaam, echte naam en privé-e-mail. Publieke repo via
  `gh repo create mrtruebeliever/<Naam> --public --source . --push`.
- Echte hardware: installeren via Dev Connect + `pebble install --cloudpebble`.

## Werkwijze per plan

Elk plan heeft milestones met een verifieerbaar tussenresultaat. Na elke
milestone: bouwen, installeren, screenshot(s) nemen en visueel controleren
voordat je verdergaat. Toon de gebruiker onderweg screenshots van de kern-
gameplay. Vraag pas om hardware-testen als de emulator-versie compleet is.
