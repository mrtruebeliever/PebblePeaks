# Pebble Peaks — tuning analysis (tools/sim_peaks.py)

> **Status (milestone 5):** voorstellen 1-4 zijn toegepast in `main.c`; `BASE` in de simulator is nu het
> nieuwe spel, `--old` geeft de oude constanten (de tabellen hieronder zijn "oud" vs. "voorstel").
> De ijsfix in main.c is breder (`s_ignore_ledge_idx`: elke knock-off, ook stenen/brokkel; sim `KNOCK_FIX`)
> en daardoor werken steentreffers nu echt (vroeger landde je meteen terug). Gemeten (decent, 300 seeds):
> alles 5 mediaan 794 m, **top 20%** (stenen uit: 44%; `ROCK_CAP=3`: 30%). Zie plan.md "Tuning (milestone 5)".

## Methode
`tools/sim_peaks.py` is een 1-op-1 Python-port van de gameplay in `main.c` (8.8 fixed point, C-delen met
afkap naar nul, zelfde tickvolgorde, 200x228, 30 fps): sprongboog, muurkaats, landingstest, vloergeneratie
met `REACH_FOR_GAP` + bonusrichels, breedtes, speciale richels, brokkel/ijs, stenen + invulnerability,
mist (incl. jasformule), edelstenen, camera, top op 1000 m met `LEDGE_SUMMIT`, `try_rope`,
`ice_slide_fp()` / `crumble_ticks()`. Verschil met het apparaat: Python-RNG i.p.v. `rand()`.

De **bot** staat op een richel, wacht een reactietijd (ticks), zoekt dan met een 3-sprongen-vooruitblik
(exacte boog-simulatie, links/rechts) de richel die het hoogst uitkomt (ijs/brokkel krijgt een straf als de
reactietijd te lang is om op tijd weg te springen), en maakt met kans `miss` de verkeerde keuze. Touw: als
de vlucht volgens simulatie op geen enkele richel meer eindigt (of bij tuimelen), drukt hij na een
touw-reactietijd op SELECT. Profielen (reactietijd in ticks / foutkans):

| profiel | reactie | fout | bedoeld als |
|---|---|---|---|
| beginner | 15-40 | 12% | eerste week |
| **decent** | 10-25 | 4% | referentiespeler |
| fast | 6-15 | 4% | het 6-15-tick-model uit de opdracht |
| expert | 4-9 | 1.5% | bijna perfect |

Gebruik: `python3 tools/sim_peaks.py --profile decent --seeds 300 [--proposal] [--diag] [--set NAAM=WAARDE]`.
Economie: 100 "carrières", greedy goedkoopste-eerst kopen, runs worden getrokken uit 40 gesimuleerde runs
per gear-staat.

## Bevindingen aan de huidige constanten (decent, 300 seeds)

| gear | mediaan m | p90 m | top % | stenen/run |
|---|---|---|---|---|
| geen | 228 | 386 | 0 | 22.5 |
| grip 5 | 223 | 440 | 0 | 25.0 |
| jas 5 | 228 | 386 | 0 | 22.7 |
| touw 5 | 387 | 507 | 0 | 43.3 |
| alles 2 | 422 | 569 | 0 | 48.5 |
| alles 3 | 489 | 660 | 0 | 58.0 |
| alles 4 | 528 | 825 | 1.0 | 66.9 |
| **alles 5** | **400** | 662 | **0.3** | 49.7 |

1. **Alles 5 is slechter dan alles 4** (ook bij fast: 454 vs 664 m; expert 490 vs 1000). Oorzaak = bug:
   **ijs-softlock bij Gripzolen 4-5.** Een klimmer die van een ijsrichel glijdt (`knock_off(±100)`)
   landt de volgende tick weer op dezelfde richel (de landingstest staat 7 px overhang toe), `x_fp = cx << FP`
   gooit de fractie weg, en bij `ice_slide_fp() + 100 < 256` (grip 5: 104+100) komt hij nooit een hele pixel
   verder: hij hangt eeuwig "airborne" aan de rand tot de mist hem pakt. (Gemeten: 800-3000 knock-offs per run.)
2. **Brokkelrichels slopen de trap.** Een brokkelrichel die de enige richel van zijn verdieping is verdwijnt
   voorgoed; val je (steen/fout/touw) onder die verdieping, dan is de weg omhoog weg, en de klimmer huppelt
   duizenden ticks heen en weer tot de mist hem pakt. Dit is de grootste run-killer in het late spel
   (bot stond tot 8000 ticks vast op een gat van 2 verdiepingen).
3. **Vallen = dood.** De camera scrolt nooit omlaag en richels onder het scherm worden gerecycled; een val
   van meer dan ~5 verdiepingen is altijd fataal. Gevolg: de mist beslist vrijwel nooit een run van een
   normaal tempo speler (klimtempo ~1,2 px/tick vs. mist 0,35 -> 0,7). **Donsjack doet daardoor
   (bijna) niets** voor decent/fast/expert (jas 5 = +0..5 m) en alleen iets voor trage spelers.
4. Touw is de enige upgrade die echt telt (+70% hoogte). Gripzolen helpt een beetje (p90).
5. Edelstenen: 55% per richel geeft decent ~22 stenen/run bij start, ~50-65 met gear.
   Tot volle uitrusting: decent **33 runs**, beginner 65, fast 28, expert 19. Top-eerste-keer: nooit voor decent.

## Voorstel (kleine, C-vriendelijke wijzigingen)

Alle waarden zitten in `PROPOSAL` (`--proposal`). Resultaten (300 seeds, vóór -> na):

| profiel | gear | mediaan m | top % |
|---|---|---|---|
| beginner | geen | 78 -> 62 | 0 -> 0 |
| beginner | jas 5 | 90 -> 88 | 0 |
| beginner | alles 5 | 309 -> 412 | 0 -> 0 |
| **decent** | geen | 228 -> 230 | 0 -> 0 |
| decent | touw 5 | 387 -> 406 | 0 -> 0 |
| decent | alles 3 | 489 -> 530 | 0 -> 0.3 |
| decent | alles 4 | 528 -> 666 | 1 -> 5 |
| **decent** | **alles 5** | 400 -> **926 (p90 1000)** | 0.3 -> **42** |
| fast | alles 4 / alles 5 | 664/454 -> 878/1000 | 17/3 -> 34/65 |
| expert | alles 5 | 490 -> 1000 | 8 -> 98 |

* Beginner eerste run ~47-62 m (mediaan), decent ~220 m.
* Top (decent): alles 5 = ~42% per poging (2-3 pogingen), alles 4 = 5%, alles 3 = 0%, zonder gear 0%. Dat is
  "alleen met volle uitrusting, dan in een paar pogingen".
* Grind (decent, greedy): 3 niveaus na 3 runs, 6 na 8, 9 na 15, 12 na 24, **alle 15 na ~35 runs**
  (p10-p90: 32-38). Fast: 29; beginner: 85 (verbetert in het echt met oefening). Eerste top bij decent rond
  run 30, dus nét vóór alles-gekocht (alles 4 heeft 5%).
* Kanttekening expert: met gear (en zelfs touw 5: 46%) is de top voor een bijna-perfecte speler te makkelijk;
  `SPECIAL_CAP`/rotskans zouden verder omhoog kunnen als dat storend is (`--set SPECIAL_CAP=45
  SPECIAL_DIV=8` kost decent-alles-5 ~20 pp).

### 1. Ijs-softlock (bug, verplicht)
```diff
+static int16_t s_no_land_idx = -1;   // ledge the climber just slid off (ice): cannot re-land on it
@@ try_jump(), try_rope() and start_run(): reset
+  s_no_land_idx = -1;
@@ game_tick, ice branch
-      if (cx2 < sl->x || cx2 > sl->x + sl->w) knock_off(s_ice_dir * 100);
+      if (cx2 < sl->x || cx2 > sl->x + sl->w) {
+        s_no_land_idx = s_standing_ledge_idx;   // must be set before knock_off() clears it
+        knock_off(s_ice_dir * 100);
+      }
@@ landing loop
-        if (l->w == 0) continue;
+        if (l->w == 0 || l->type == LEDGE_GONE || i == s_no_land_idx) continue;
@@ when landing succeeds (next to s_standing_ledge_idx = i;)
+          s_no_land_idx = -1;
```

### 2. Brokkelrichels komen terug (3 s) — de grote winst
```diff
-typedef enum { LEDGE_NORMAL, LEDGE_CRUMBLE, LEDGE_ICE, LEDGE_SUMMIT } LedgeType;
+typedef enum { LEDGE_NORMAL, LEDGE_CRUMBLE, LEDGE_ICE, LEDGE_SUMMIT, LEDGE_GONE } LedgeType;
+#define CRUMBLE_RESPAWN_TICKS 90   // a crumbled ledge is back after 3 s (no stranded climbers)
@@ game_tick, crumble loop
   for (int i = 0; i < MAX_LEDGES; i++) {
     Ledge *l = &s_ledges[i];
-    if (l->w == 0 || l->type != LEDGE_CRUMBLE || l->crumble_ticks < 0) continue;
-    l->crumble_ticks--;
-    if (l->crumble_ticks < 0) {
-      bool climber_here = !s_airborne && s_standing_ledge_idx == i;
-      l->w = 0;
-      if (climber_here) knock_off(0);
-    }
+    if (l->w == 0) continue;
+    if (l->type == LEDGE_GONE) {          // waiting to come back
+      if (--l->crumble_ticks < 0) { l->type = LEDGE_CRUMBLE; l->crumble_ticks = -1; }
+      continue;
+    }
+    if (l->type != LEDGE_CRUMBLE || l->crumble_ticks < 0) continue;
+    if (--l->crumble_ticks < 0) {
+      bool climber_here = !s_airborne && s_standing_ledge_idx == i;
+      l->type = LEDGE_GONE;
+      l->crumble_ticks = CRUMBLE_RESPAWN_TICKS;
+      if (climber_here) knock_off(0);
+    }
   }
```
Ook nodig: `try_rope()` (beide ledge-selecties) en de tekenlus slaan `LEDGE_GONE` over (evt. alleen een
gestippelde stomp tekenen). Zoek op `->w != 0` / `w == 0` in main.c. `pick_ledge_type` blijft ongewijzigd.

### 3. Mist: duidelijker bedreiging voor trage spelers, jas telt mee
```diff
-#define MIST_BASE_SPEED_FP 90      // 0.35 px/tick
-#define MIST_MAX_SCALE_FP 878
+#define MIST_BASE_SPEED_FP 150     // 0.59 px/tick at the start
+#define MIST_MAX_SCALE_FP 450      // cap: 1.03 px/tick
-  int32_t scale_fp = 256 + (s_height_m * 26) / 100;
+  int32_t scale_fp = 256 + (s_height_m * 20) / 100;
-  int32_t mist_speed_fp = (MIST_BASE_SPEED_FP * scale_fp * (100 - 4 * s_jacket)) / (100 * 256);
+  int32_t mist_speed_fp = (MIST_BASE_SPEED_FP * scale_fp * (100 - 6 * s_jacket)) / (100 * 256);
-  s_mist_top_fp = (int32_t)(s_base_y + MIST_GAP_START + s_jacket * 20) << FP;
+  s_mist_top_fp = (int32_t)(s_base_y + MIST_GAP_START + s_jacket * 30) << FP;
```
(Jas 5 = mist 30% trager en 150 px verder weg. Effect blijft bescheiden: beginner jas 5 +40% hoogte,
decent/fast ~0, omdat vallen de echte killer is; een écht nuttige jas voor goede spelers vraagt een
spelregelwijziging, bv. mist ook laten stijgen terwijl je valt/camera laten meevallen. Bewust niet voorgesteld.)
Pas plan.md-tekst aan (0,35 px/tick, +10%/100 m, max 1,2) of laat deze stap weg: 1 en 2 doen het zware werk.

### 4. Edelstenen
```diff
-  if (rand() % 100 >= 55) return;
+  if (rand() % 100 >= 40) return;
```
Met de betere runs (meer verdiepingen) blijft het grind-doel ~35 runs voor decent staan.

## Niet onderzocht / open
* Gripzolen blijven zwak (decent grip 5 = +10 m mediaan, p90 +200 m na de fixes): ijs/brokkel kunnen met 70% normale richels
  ontweken worden. Optie: `SPECIAL_CAP` 30 -> 40 en `SPECIAL_DIV` 10 -> 8 (maakt grip waardevoller, maar de
  top moeilijker: decent alles 5 ~19-30%).
* De bot kijkt perfect vooruit en weet nooit "geen richel erboven"; menselijke spelers zullen lager scoren.
  Gebruik de verhoudingen (touw +70%, alles 5 ≈ 2x geen) meer dan de absolute meters.
* Stenen: zonder tuning 14 treffers/run bij alles 5; rotskans verlagen (`ROCK_CAP` 3) veranderde weinig.

## Touwrollen en edelsteenrichels (oktober 2026)
* Losse touwrollen: 0,7% per richel vanaf 20 m (`COIL_PERMILLE 7`), max 9 touwen bij je. 2% bleek te
  royaal: geen uitrusting 218 -> 291 m, alles 5 top 30% -> 53%. Met 0,7% (120 seeds, decent):
  geen uitrusting 238 m eerste run, touw 5 ~422 m, alles 5 mediaan 890 m en top 37,5%; ~35 runs om alles te kopen.
* Edelsteenrichels (2 edelstenen + 1 touwrol, alleen met touw over) zitten niet in de sim: de bot richt niet.
  Ze kosten netto geen touw bij een treffer, alleen tijd tegenover de mist.
