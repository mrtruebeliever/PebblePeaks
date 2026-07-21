# Pebble Peaks

Verticale klimmer voor de Pebble Time 2 (emery) — het zusje van
[PebbleDiver](https://github.com/mrtruebeliever/PebbleDiver).

Zie [`plan.md`](plan.md) voor het volledige ontwerp en [`COMMON.md`](COMMON.md)
voor de gedeelde projectconventies.

**Status:** milestone 1 (skelet + klimmer + richels + sprongfysica) en
milestone 2 (camera-scroll + procedurele richels + hoogteteller + mist +
game-over) staan in `src/c/main.c`. Milestone 1 is volledig geverifieerd in
de emulator (screenshots, sprongboog met press.py getest). Milestone 2 is
gebouwd en de kernlogica (richel-generatie + landingsfysica) is rigoureus
geverifieerd via een losse Python-simulatie van de exacte fixed-point-fysica
(500 seeds × 30 verdiepingen, 0 mislukkingen) en meermaals bevestigd op het
scherm (klimmer bereikte 3m en 6m met correcte landingen). Een emulator-
display-storing aan het einde van deze sessie verhinderde de laatste
doorlopende visuele speeltest — dat staat nog open voor een volgende sessie.

Nog te doen: milestone 3 (edelstenen, brokkel-/ijsrichels, vallende stenen,
moeilijkheidscurve), milestone 4 (winkel/upgrades/victory), milestone 5
(i18n + tuning + release).
