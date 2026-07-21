# Pebble Peaks

Verticale klimmer voor de Pebble Time 2 (emery) — het zusje van
[PebbleDiver](https://github.com/mrtruebeliever/PebbleDiver).

Zie [`plan.md`](plan.md) voor het volledige ontwerp en [`COMMON.md`](COMMON.md)
voor de gedeelde projectconventies.

**Status:** milestone 1 (skelet + klimmer + richels + sprongfysica),
milestone 2 (camera-scroll + procedurele richels + hoogteteller + mist +
game-over) en milestone 3 (edelstenen, brokkel-/ijsrichels, vallende stenen,
moeilijkheidscurve) staan in `src/c/main.c`. Milestone 1 is volledig
geverifieerd in de emulator (screenshots, sprongboog met press.py getest).
Milestone 2's kernlogica (richel-generatie + landingsfysica) is rigoureus
geverifieerd via een losse Python-simulatie van de exacte fixed-point-fysica
(500 seeds × 30 verdiepingen, 0 mislukkingen) en bevestigd op het scherm
(klimmer bereikte 3m en 6m met correcte landingen). Milestone 3 bouwt
(schoon, RAM-budget ruim binnen limiet) en is zorgvuldig doorgenomen op
correctheid, maar nog niet visueel getest in de emulator — een aanhoudende
emulator/display-storing (WSL-kant) verhinderde dit aan het einde van deze
sessie. Edelstenen-totaal en hoogterecord persisten al (`PK_GEMS`/`PK_BEST`),
klaar voor de winkel in milestone 4.

Nog te doen: emulator-speeltest van milestone 3, milestone 4
(winkel/upgrades/victory), milestone 5 (i18n + tuning + release).
