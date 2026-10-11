# Pebble Peaks — verticale klimmer (zusje van PebbleDiver)

Lees eerst `COMMON.md`. Projectmap: `PebblePeaks/`, app-naam
"Pebble Peaks". Dit spel hergebruikt bewust de PebbleDiver-architectuur —
kopieer main.c als skelet en vervang de gameplay-kern. Zelfde
state-machine, shop, i18n-patroon, persist-patroon, input-lock, auto-pauze.

## Concept

De omgekeerde PebbleDiver: in plaats van dalen klim je. Een klimmer springt
van richel naar richel omhoog langs een bergwand terwijl van onderaf ijskoude
mist opstijgt. Verzamel edelsteentjes (valuta), koop uitrusting, en haal het
hoogterecord. Endgame: op **1000 m** staat de top met een vlag — realistisch
alleen haalbaar met volledige uitrusting (spiegel van de magische Pebble).

## Besturing (twee knoppen, geen vasthouden)

- **UP** = sprong naar links-omhoog, **DOWN** = sprong naar rechts-omhoog.
  Vaste sprongboog (dx ≈ ±34 px, dy ≈ −44 px top), single clicks.
- Springen kan alleen vanaf een richel (of: één "coyote-tick" van 3 frames na
  het aflopen van een rand). In de lucht is de richting niet meer te sturen —
  het spel draait om het kiezen van de juiste richel.
- SELECT = (met touwupgrade) noodgreep: één keer per run terugveren naar de
  laatst verlaten richel. BACK = pauze.
- Muren: linker/rechter schermrand kaatst de sprong terug (wall-bounce, halve
  dx), zodat een "verkeerde" sprong niet altijd dodelijk is.

## Wereld & scroll

- Camera volgt de klimmer omhoog: zodra hij boven 40% van het scherm komt,
  scrollt de wereld omlaag (klassiek Icy Tower). Hoogte in meters =
  totale scroll / 12 (zelfde schaal als PebbleDiver-diepte).
- **Mist** stijgt van onder: begint 60 px onder de klimmer, stijgt 0,59 px/tick,
  versnelt met hoogte (+8% per 100 m, max 1,03 px/tick). Klimmer in de mist =
  game over ("Bevroren!"). Mist tekenen als LightGray-band met golvende
  bovenrand (sinuslijn) en 30%-dither erboven (om de 2 px een pixel).
- Richels: procedureel per "verdieping" (om de 34–44 px hoogte 1–2 richels,
  breedte 30–60 px, horizontale positie random maar altijd bereikbaar vanaf
  de vorige verdieping — check met de vaste sprongboog; zo niet, corrigeer).
  Hoger = smaller en verder uit elkaar.
- Speciale richels vanaf 150 m: brokkelrichel (verdwijnt 0,8 s na landing,
  bruin, trilt eerst; komt 3 s later terug, intussen een gestippelde omtrek), ijsrichel (klimmer glijdt 1,5 px/tick naar de richelrand,
  Celeste-kleur). Vanaf 300 m: vallende stenen (spawn boven, recht omlaag,
  raak = terugvallen naar richel eronder + korte invulnerability).
- Achtergrond: lucht wordt donkerder met hoogte (VividCerulean → CobaltBlue →
  DukeBlue → OxfordBlue + sterren vanaf 600 m), bergsilhouet-parallax
  (donkere driehoeken die op halve snelheid meescrollen).

## Klimmer & animatie

Primitives zoals de duiker: lijfje (rood jack, fill_rect rond), hoofd met
muts (cirkel + pompon-pixel), beentjes als 2 lijntjes die wisselen bij
landing/sprong. Springboog met 2–3 tussenposes (benen gestrekt), landing
= 1 frame plat. Edelsteentjes: ruitvorm (gpath 4 punten) in wisselende
kleuren, zweven boven sommige richels.

## Economie & winkel (PebbleDiver-model)

Valuta: edelstenen. Drie upgrades × 5 levels, kosten 15/40/90/160/250:
- **Gripzolen** — minder ijsglijden en brokkelrichels houden 0,15 s langer.
- **Donsjack** — de mist start 30 px verder weg en stijgt 6% trager per level.
- **Klimtouw** — SELECT-noodgreep: 1 gebruik per run per level (max 5).
Alles max → de top op 1000 m is haalbaar; vlag pakken = ST_VICTORY
("Je hebt de top bereikt!"), trofee-vlaggetje op het titelscherm, persist
PK_FOUND-equivalent. Zelfde 1 s input-lock na game-over/victory.

## HUD

Zwarte bovenbalk: hoogte "312m" links, mist-afstandsbalkje midden (hoe vol,
hoe dichterbij — rood onder 30 px), edelstenen rechts (ruitje + getal).

## i18n

5 talen, PebbleDiver-patroon. Kern-strings: titel "Pebble Peaks" (alle talen
gelijk), "Bevroren!"/"Frozen!"/"Gelé !"/"Erfroren!"/"¡Congelado!",
hoogte/edelstenen-regels, winkelnamen (Gripzolen/Donsjack/Klimtouw — EN
Grip soles/Down jacket/Climbing rope, let op DE-breedtes:
Griffsohlen/Daunenjacke/Kletterseil), "Je hebt de top bereikt!".

## Milestones

1. ✅ **Klaar, emulator-geverifieerd.** Skelet uit PebbleDiver overnemen;
   klimmer + richels + sprongfysica op een statisch scherm (nog geen
   scroll) → screenshot, sprong-boog gecheckt met press.py (up/down).
2. ✅ **Klaar, gebouwd + geverifieerd** (screenshots tot 6m, plus een
   500-seed/30-verdieping Python-simulatie van de exacte fixed-point-fysica
   met 0 mislukkingen). Camera-scroll + procedurele richels + hoogte-teller
   + mist + game over.
3. ✅ **Klaar, gebouwd + code-review, weergave geverifieerd** (2026-10-11,
   emery-emulator, tijdelijke build met `SPECIAL_LEDGE_MIN_M` en
   `ROCK_MIN_M` op -300): edelstenen in meerdere kleuren, ijsrichels
   (Celeste), brokkelrichels (BulgarianRose) en vallende stenen staan
   allemaal in beeld. Het **gedrag** (ijs glijdt, brokkelrichel trilt en
   verdwijnt, steen slaat je terug) is nog niet gezien: dat vraagt gericht
   springen, geen blind up/down. Edelstenen + speciale richels + vallende
   stenen + moeilijkheidscurve.
4. ✅ **Klaar, emulator-geverifieerd** (2026-10-11, TEMP-build met
   upgrades in RAM, save uit, TEMP-regels daarna verwijderd). Winkel
   (Gripzolen/Donsjack/Klimtouw, 15/40/90/160/250), Klimtouw op SELECT
   (alleen in de lucht; terug naar de laatst verlaten richel, anders de
   dichtstbijzijnde richel eronder boven de mist), **fysieke vlag** op een
   brede topricht (`LEDGE_SUMMIT`, 64 px) zodra een verdieping `SUMMIT_M`
   haalt: landen op de top → klimmer waggelt naar de vlag → `ST_VICTORY`;
   mist stopt op de top. Persist 3–6 = grip/jacket/rope/found. Titel met
   record, edelstenen en trofee-vlag; game-over met "Nieuw record!";
   pauze-BACK bankt de edelstenen. Gezien in de emulator: winkel, ijsglijden
   (val-pose), klimtouw, top + overwinningsscherm. Niet gezien: brokkelrichel
   die verdwijnt, steen die je raakt.
   **Graphics**: klimmer en vlag zijn pixel-art sprites in begin-jaren-90
   Sega-stijl (`tools/sprites.py` → genereert `src/c/sprites.h`; poses
   IDLE/LAND/JUMP/FALL, gespiegeld voor links). Richels: sneeuw op grijze
   rots, ijs met ijspegels, gebarsten bruine brokkelrichel. Antialiasing uit.
5. i18n + visuele taalcheck (DE) + tuning-speeltest; screenshots + GIF;
   release-fase na akkoord.

## Implementatienotities voor milestone 4 (stand van zaken in main.c)

- **State-enum** is nu `ST_TITLE / ST_PLAYING / ST_PAUSED / ST_GAMEOVER`.
  Milestone 4 voegt `ST_SHOP` en `ST_VICTORY` toe.
- **Persist-keys**: `PK_GEMS`=1 (levenslang edelstenen-totaal, opgeteld bij
  elke game-over) en `PK_BEST`=2 (all-time hoogterecord) bestaan al en
  worden geladen/opgeslagen in `load_progress()`/`save_progress()`. Nieuwe
  keys voor de drie upgrade-levels en de victory-vlag (PebbleDiver's
  PK_FOUND-equivalent) moeten bij 3, 4, 5, 6 beginnen.
- **SELECT doet nog niets tijdens ST_PLAYING** — dat is precies de haak
  voor de Klimtouw-noodgreep (milestone 4): terugveren naar de laatst
  verlaten richel, 1×/run/level.
- **Tunable constants die per upgrade-level moeten variëren** (nu vaste
  `#define`s, moeten runtime-waarden worden op basis van upgrade-level):
  `ICE_SLIDE_FP` (1.5 px/tick, Gripzolen vertraagt dit), `CRUMBLE_TICKS`
  (24 ticks/0.8s, Gripzolen verlengt met 0,15s/level), en de mist-formule
  in `game_tick` (`MIST_GAP_START`/`MIST_BASE_SPEED_FP`/schaal-berekening,
  Donsjack vergroot de startafstand en vertraagt de stijging 4%/level — sinds
  milestone 5: 30 px en 6%/level).
- **Moeilijkheidscurve-keuzes** (niet expliciet gespecificeerd in het plan,
  zelf gekozen, hou hier rekening mee bij tuning): speciale-richel-kans
  = `(hoogte_m - 150) / 10`%, capped op 30%; vallende-stenen-kans
  = `1 + (hoogte_m - 300) / 60`%, capped op 6%, met een cooldown van
  40 ticks tussen spawns.
- **1000m/victory**: plan.md zegt "vlag pakken" bij 1000m — nog te
  beslissen of dit een fysiek vlag-object is (zoals PebbleDiver's magische
  Pebble-entiteit, met botsingsdetectie) of gewoon het bereiken van
  `s_height_m >= 1000` triggert. Eerste optie past beter bij het bestaande
  entity/collision-patroon (gems/rocks) en voelt tastbaarder.

## Testnotities

Volledig met press.py testbaar: `up`/`down` clicks om te springen (kopie
staat al in `tools/press.py`). Voor de GIF: afwisselend up/down met sleeps.
Game over forceren: niets doen (de mist start 60 px lager en stijgt sinds milestone 5 0,59 px/tick, dus hij haalt je in ~3,5 s; met Donsjack 5 ~15 s). Victory testen
met RAM-override (upgrades max + top op 30 m TIJDELIJK) — zie de
TEMP-procedure in COMMON.md.

⚠️ **Les uit milestone 2/3**: omdat richel-richting en -positie nu
procedureel/random zijn (niet meer een vast alternerend up/down-patroon
zoals in milestone 1), is blind afwisselend `up`/`down` drukken met
press.py **geen betrouwbare test meer** voor reachability-logica — je moet
ofwel de gegenereerde richting aflezen (tijdelijke debug-tekst op het
scherm, zoals gedaan in milestone 2) ofwel — veel betrouwbaarder en
sneller — de generatie-/fysica-logica **na-simuleren in een los
Python-script** met exact dezelfde integer/fixed-point-rekensom, en die
over honderden willekeurige seeds laten lopen. Gebruik dat als eerste
check bij nieuwe procedurele/probabilistische mechanica, vóór (of in
plaats van) handmatige emulatortests. De pypkjs/qemu-emulatorverbinding
via press.py is bovendien af en toe onbetrouwbaar gebleken (timeouts,
verkeerde app in de launcher gefocust, een enkele keer een vastgelopen
qemu-proces dat een herstart nodig had) — reken niet blind op de eerste
klik/screenshot na een `pebble install`, en wees bereid de emulator
opnieuw op te starten als install/screenshot een paar keer achter elkaar
timeout geeft.

## Tuning (milestone 5)

Toegepast uit `tools/TUNING.md` (simulator `tools/sim_peaks.py`, nu gelijk aan main.c; `--old` = vóór de tuning):
- **Knock-off-fix**: wie van een richel wordt gegooid (ijs, steen, brokkel) kan niet direct weer op díe
  richel landen (`s_ignore_ledge_idx`). Dekt de ijs-softlock bij Gripzolen 4-5 én maakt stenen echt
  gevaarlijk (eerder landde je na een treffer meteen weer op dezelfde richel). Op de top raken stenen niet.
- **Brokkelrichels komen terug**: `LEDGE_GONE`, na `CRUMBLE_RESPAWN_TICKS` 90 (3 s) weer heel; niet
  landbaar, geen touwdoel, getekend als gestippelde omtrek (laatste 0,5 s dichter/wit).
- **Mist**: 0,59 px/tick start (`MIST_BASE_SPEED_FP` 150), +8%/100 m (slope 20), max 1,03 px/tick
  (`MIST_MAX_SCALE_FP` 450). Donsjack: 30 px/level verder weg, 6%/level trager.
- **Edelstenen**: 40% kans per richel (was 55%).

Simulatie (decent-profiel, 300 seeds): geen gear mediaan 230 m; touw 5 408 m; alles 4 633 m (top 3%);
**alles 5 794 m, top 20%**; alle 15 levels na ~36 runs (p10-p90 33-38), eerste top rond run 33.
De analist voorspelde ~42% top bij alles 5: dat verschil komt van de stenen, die in zijn model door de
re-land-bug vrijwel onschadelijk waren (met stenen uit: 44%). Daarom is de
stenen-kans gemaximeerd op 3% i.p.v. 6% (`ROCK_CAP` 3): **alles 5 → mediaan 840 m, top 31%** (200 seeds),
d.w.z. met volle uitrusting gemiddeld een derde poging. Nog te bevestigen met een speeltest op hardware.
