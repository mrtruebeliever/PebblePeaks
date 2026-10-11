# Pebble Peaks

Een verticale klimmer voor de Pebble Time 2 (emery), het zusje van
[PebbleDiver](https://github.com/mrtruebeliever/PebbleDiver).

Spring van richel naar richel een berg op terwijl van onderaf ijskoude mist
opstijgt. **OMHOOG** springt naar linksboven, **OMLAAG** naar rechtsboven,
langs een vaste boog: in de lucht stuur je niet meer bij. Haal je de mist je
in, dan ben je bevroren.

Het doel: op **1000 m** staat de top met een vlag. Land op de top, loop naar
de vlag en pak hem; daarna staat de vlag naast je klimmer op het titelscherm.
Realistisch lukt dat alleen met volledige uitrusting.

## Gameplay

- **Richels:** besneeuwde rots; vanaf 150 m ook **ijs** (je glijdt naar de
  rand) en **brokkelrichels** (trillen, zakken door en verdwijnen, en groeien
  na 3 s terug). Hoe hoger, hoe smaller en verder uit elkaar.
- **Vallende stenen** vanaf 300 m: een treffer gooit je van je richel.
- **Mist** stijgt steeds sneller naarmate je hoger komt.
- De lucht wordt donkerder met de hoogte, met sterren vanaf 600 m.
- **Edelstenen** zijn de valuta en blijven bewaard tussen runs (ook als je
  stopt via pauze → TERUG).

## Winkel (OMLAAG vanaf titel of game-over)

Drie upgrades, elk 5 niveaus, kosten 15/40/90/160/250 edelstenen:

- **Gripzolen:** minder glijden op ijs, brokkelrichels houden langer.
- **Donsjack:** de mist begint verder weg en stijgt trager.
- **Klimtouw:** druk SELECT in de lucht om terug op je laatste richel
  getrokken te worden; één keer per run per niveau.

## Talen

De app volgt de taal van het horloge: Engels (standaard), Nederlands, Frans,
Duits en Spaans. De vertaaltabel staat bovenin `src/c/main.c`.

## Ontwikkeling

- `tools/sprites.py`: alle pixel-art (klimmer, vlag, edelstenen, stenen,
  iconen); genereert `src/c/sprites.h`.
- `tools/sim_peaks.py`: Python-port van de spellogica met een bot-speler voor
  balans-tests; resultaten en keuzes in `tools/TUNING.md`.
- `tools/gen_icon.py`, `tools/gen_marketing.py`: menu-icoon en store-art.
- `tools/release.py`: bouwt `build/PebblePeaks-release.pbw` voor de store.
- `tools/press.py`: knoppen in de emulator drukken.

Zie [`plan.md`](plan.md) voor het ontwerp en [`COMMON.md`](COMMON.md) voor de
gedeelde projectconventies.

## Licentie

MIT, zie [`LICENSE`](LICENSE).
