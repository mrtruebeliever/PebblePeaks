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
- **Mist** stijgt van onder: begint 60 px onder de klimmer, stijgt 0,35 px/tick,
  versnelt met hoogte (+10% per 100 m, max 1,2 px/tick). Klimmer in de mist =
  game over ("Bevroren!"). Mist tekenen als LightGray-band met golvende
  bovenrand (sinuslijn) en 30%-dither erboven (om de 2 px een pixel).
- Richels: procedureel per "verdieping" (om de 34–44 px hoogte 1–2 richels,
  breedte 30–60 px, horizontale positie random maar altijd bereikbaar vanaf
  de vorige verdieping — check met de vaste sprongboog; zo niet, corrigeer).
  Hoger = smaller en verder uit elkaar.
- Speciale richels vanaf 150 m: brokkelrichel (verdwijnt 0,8 s na landing,
  bruin, trilt eerst), ijsrichel (klimmer glijdt 1,5 px/tick naar de richelrand,
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
- **Donsjack** — de mist start verder weg en stijgt 4% trager per level.
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

1. Skelet uit PebbleDiver overnemen; klimmer + richels + sprongfysica op een
   statisch scherm (nog geen scroll) → screenshot, sprong-boog checken met
   press.py (up/down) en 2 screenshots.
2. Camera-scroll + procedurele richels + hoogte-teller + mist + game over.
3. Edelstenen + speciale richels + vallende stenen + moeilijkheidscurve.
4. Winkel + upgrades + top/victory (testen met RAM-cheat volgens COMMON.md,
   cheats daarna verwijderen!) + title/pauze/records.
5. i18n + visuele taalcheck (DE) + tuning-speeltest; screenshots + GIF;
   release-fase na akkoord.

## Testnotities

Volledig met press.py testbaar: `up`/`down` clicks om te springen. Voor de
GIF: afwisselend up/down met sleeps. Game over forceren: niets doen (mist
haalt je in ~30 s). Victory testen met RAM-override (upgrades max +
top op 30 m TIJDELIJK) — zie de TEMP-procedure in COMMON.md.
