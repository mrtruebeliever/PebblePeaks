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
- **Klimtouw:** een enterhaak, één keer per run per niveau.
  - SELECT in de lucht: de haak grijpt een richel midden op het scherm en
    je klimt langs het touw omhoog.
  - SELECT op een richel: richten, SELECT schiet, TERUG annuleert. Twee
    richtstijlen, te wisselen met OMHOOG in het pauzescherm: een slingerend
    vizier (timing) of kantelen. Zo haal je edelsteenrichels die te ver weg
    hangen om te springen; ze dragen 2 edelstenen en een touwrol, zodat je
    er altijd weer af kunt haken. Mis je, dan ben je het touw kwijt.
  - Losse touwrollen (zeldzaam) geven +1 touw, tot 9 tegelijk.

## Instellingen (OMHOOG vanaf het titelscherm)

- **Verlichting:** uit (het horloge regelt het), time-out (aan tijdens het
  spelen, uit 30 s na de laatste knop) of altijd.
- **Lichteffecten:** het licht kleurt mee met de lucht, pulseert rood bij de
  mistwaarschuwing en kleurt goud op de top; bij die twee gaat het licht ook
  even aan.
- **Trilling:** aan/uit.
- **Geluid:** uit/zacht/normaal/luid. Quiet Time en de mute van het horloge
  gaan altijd voor.
- **Richten:** slinger of kantelen.
- **Wissen:** edelstenen, record, uitrusting en de topvlag (twee keer
  drukken); de instellingen blijven.

Tijdens het klimmen wisselt het pauzescherm (TERUG) richten met OMHOOG en
geluid met OMLAAG.

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
- **Emulator:** bouw met `PEAKS_MUTE=1 pebble build`. De emulator heeft geen
  speaker en de app stopt dan bij het eerste geluid. Een gewone
  `pebble build` maakt weer de versie met geluid (voor het horloge en de store).

Zie [`plan.md`](plan.md) voor het ontwerp en [`COMMON.md`](COMMON.md) voor de
gedeelde projectconventies.

## Licentie

MIT, zie [`LICENSE`](LICENSE).
