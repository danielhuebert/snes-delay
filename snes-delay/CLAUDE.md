# CLAUDE.md – logue-SDK-Projekt (Korg minilogue xd)

Dieses Projekt ist eine Custom Unit (Oszillator oder Effekt) für den **Korg minilogue xd**,
gebaut mit Korgs **logue SDK v1** (https://github.com/korginc/logue-sdk).
Der Nutzer arbeitet auf einem **Mac (Apple Silicon möglich)** mit **VS Code** und baut mit **Docker**.
Antworte auf Deutsch.

## Projektaufbau (v1, minilogue xd)

Ein Projektordner enthält immer alle diese Dateien, sonst schlägt der Build fehl:

```
<projekt>/
├── Makefile          # von Korg, nicht anfassen (außer PLATFORMDIR)
├── project.mk        # PROJECT-Name + Quelldateien (UCXXSRC = foo.cpp)
├── manifest.json     # Plattform, Modultyp, Anzeigename, Edit-Parameter
├── ld/               # Linker-Skripte: userosc.ld | usermodfx.ld | userdelfx.ld | userrevfx.ld, rules.ld, *_api.syms
├── tpl/_unit.c       # Hook-Tabelle, nicht anfassen
└── foo.cpp           # eigener DSP-Code
```

- `Makefile` setzt `PLATFORMDIR ?= $(abspath ..)`. Der Projektordner muss deshalb unter
  `logue-sdk/platform/minilogue-xd/<projekt>/` liegen, oder `PLATFORMDIR` muss beim Aufruf gesetzt werden.
- Fehlt `manifest.json`, meldet der Docker-Build `[Err] Unknown platform`.
- `PROJECT` in `project.mk` bestimmt den **Dateinamen** (`<PROJECT>.mnlgxdunit`),
  `"name"` in `manifest.json` den **Namen auf dem Display**. Die beiden dürfen verschieden sein
  (Beispiel: Datei `triangle.mnlgxdunit`, Anzeige „nestri“).
- Neues Projekt: Template `platform/minilogue-xd/dummy-osc` (bzw. `dummy-modfx`, `dummy-delfx`, `dummy-revfx`)
  kopieren, umbenennen, `project.mk` und `manifest.json` anpassen. Gutes Beispiel mit Parametern: `waves/`.
- Build-Artefakte: `build/` und `*.mnlgxdunit` gehören in `.gitignore`.

## manifest.json

```json
{
  "header": {
    "platform": "minilogue-xd",
    "module": "osc",            // osc | modfx | delfx | revfx
    "api": "1.1-0",
    "dev_id": 0, "prg_id": 0,
    "version": "1.0-0",
    "name": "nestri",           // kurz halten, wird am Gerät angezeigt
    "num_param": 0,             // nur osc: 0–6 Edit-Parameter
    "params": []                // ["Name", min, max, "%"|""], min/max in -100..100
  }
}
```

- Parametername: ca. 10 Zeichen sichtbar. Typ `""` = typenlos (min/max ≥ 0, Anzeige um 1 verschoben: 0–9 wird 1–10).
- Effekte haben keine Edit-Parameter (`num_param: 0`), nur TIME und DEPTH (delfx/revfx zusätzlich SHIFT+DEPTH).
- Firmware ≥ 2.00 auf dem minilogue xd nötig für API 1.1-0.

## Oszillator-API (`#include "userosc.h"`)

Hooks (alle müssen existieren):

```cpp
void OSC_INIT(uint32_t platform, uint32_t api);
void OSC_CYCLE(const user_osc_param_t * const params, int32_t *yn, const uint32_t frames);
void OSC_NOTEON(const user_osc_param_t * const params);
void OSC_NOTEOFF(const user_osc_param_t * const params);
void OSC_PARAM(uint16_t index, uint16_t value);
```

- `params->pitch`: High-Byte = MIDI-Note, Low-Byte = Feinstimmung (0–255).
  Phaseninkrement pro Sample: `osc_w0f_for_note(pitch >> 8, pitch & 0xFF)`.
- `params->shape_lfo`: q31-LFO-Wert, der **zusätzlich** auf SHAPE wirkt:
  `clip01f(s_shape + q31_to_f32(params->shape_lfo))`.
- Ausgabe: `yn` ist q31, also `*(y++) = f32_to_q31(sig)` mit `sig` in [-1, 1]. Etwas Headroom (z. B. `0.9f *`) lassen.
- `OSC_PARAM` Indizes: `k_user_osc_param_id1..id6` (Edit-Menü), `k_user_osc_param_shape`, `k_user_osc_param_shiftshape`.
  SHAPE/SHIFT+SHAPE-Werte sind 10 Bit (0–1023): `param_val_to_f32(value)` liefert 0..1.
  Edit-Parameter kommen direkt im Bereich aus `manifest.json` an.
- Zustand in `static`-Variablen halten; in `OSC_INIT` zurücksetzen. Phase in [0,1) halten: `phase -= (uint32_t)phase;`.
- Abtastrate 48 kHz (`k_samplerate`). Code + Daten: max. **32 KB** (SRAM, `ld/userosc.ld`).

## Effekt-APIs (Kurzfassung)

- modfx (`usermodfx.h`): `MODFX_PROCESS(const float *main_xn, float *main_yn, const float *sub_xn, float *sub_yn, uint32_t frames)`,
  Stereo interleaved. 6 KB SRAM für Code, 128 KB SDRAM für Puffer.
- delfx/revfx (`userdelfx.h` / `userrevfx.h`): `DELFX_PROCESS(float *xn, uint32_t frames)` bzw. `REVFX_PROCESS`, in place, Stereo interleaved.
  12 KB SRAM für Code, ca. 2,4 MB SDRAM.
- Stereo-Interleaving in `xn`: L0, R0, L1, R1, … → `x += 2` pro Frame.
- Große Puffer immer ins SDRAM legen: `static float s_buf[N] __sdram;`
- Kleine Zustandsarrays (FIR-History, Interpolationszustand) im SRAM lassen (kein `__sdram`).
- Parameter: `*_PARAM(uint8_t index, int32_t value)`, Wert ist q31 → `q31_to_f32(value)` ergibt 0..1.
- delfx-Parameterindizes: `k_user_delfx_param_time` (0), `k_user_delfx_param_depth` (1),
  `k_user_delfx_param_shift_depth` (2).
- revfx-Parameterindizes: analog mit `k_user_revfx_param_*`.
- Weitere Hooks: `*_INIT`, `*_SUSPEND`, `*_RESUME`.
- `*_RESUME`: per-Sample-Zustandsspeicher (z. B. FIR-History, Ringpuffer-Index) leeren,
  sonst Knackser beim Wiedereinschalten des Effekts.
- `float * __restrict` für den `xn`-Pointer im Process-Hook empfehlenswert (kein Aliasing).

## DSP-Regeln für den Cortex-M4 (Echtzeit)

- Nur `float` (single precision). Literale mit `f` (`0.5f`); der Build setzt `-fsingle-precision-constant`.
- Keine dynamische Speicherverwaltung, keine Exceptions, kein `printf`, keine STL-Container.
- Teure Rechnungen (`powf`, `expf`, Tabellenaufbau) aus der Sample-Schleife heraus in den Block-Anfang oder in `OSC_PARAM` ziehen.
- Schnelle Helfer aus `inc/utils/float_math.h` bevorzugen: `clip01f`, `clip1m1f`, `si_roundf`, `si_floorf`,
  `fastpow2f`, `fastersinf`, `linintf`, `dbampf` …
- Aus `osc_api.h`: bandbegrenzte Wellen `osc_bl2_sawf/sqrf/parf` (gegen Aliasing), `osc_sinf`, `osc_white()`,
  Wavetables `wavesA..F` mit `osc_wave_scanf`, Sättigung `osc_softclipf`, `osc_sat_cubicf`.
  DSP-Bausteine: `inc/dsp/biquad.hpp`, `delayline.hpp`, `simplelfo.hpp`.
- Naive Wellenformen (Dreieck, Rechteck, Stufen) aliasen in hohen Lagen; für saubereren Klang bandbegrenzte Varianten verwenden.

## Bauen

SDK einmalig mit Submodulen klonen (die GitHub-ZIP reicht nicht, CMSIS fehlt dort):

```sh
git clone https://github.com/korginc/logue-sdk.git
cd logue-sdk && git submodule update --init
cd docker && ./build_image.sh && cd ..      # Image lokal bauen, läuft nativ auf Apple Silicon
```

Build (Docker Desktop muss laufen), aus dem Ordner `logue-sdk`:

```sh
docker/run_cmd.sh build minilogue-xd/<projekt>
```

Erfolg: `Deploying to /workspace/minilogue-xd/<projekt>/<PROJECT>.mnlgxdunit` und `Done`.
`/workspace` im Container ist der Ordner `logue-sdk/platform` auf dem Mac. Ergebnis im Finder öffnen:
`open platform/minilogue-xd/<projekt>`.

Ohne Docker: `cd platform/minilogue-xd/<projekt> && make install GCC_BIN_PATH=<pfad-zu-arm-none-eabi-bin>`.
Alle verfügbaren Projekte im Container: `docker/run_interactive.sh`, dann `build -l --minilogue-xd`.

Bekannte, harmlose Meldung: `cc1: warning: option '-mstructure-size-boundary' is deprecated`.
Die Codegröße steht in der `text`-Spalte der Größenausgabe; für Oszillatoren unter 32 KB bleiben.

## Auf das Gerät laden und testen

1. **minilogue xd Sound Librarian** öffnen, `.mnlgxdunit` auf einen freien Platz unter *User Oscillators* (bzw. *User Effects*) ziehen, übertragen.
   Alternative: `logue-cli` aus `tools/logue-cli`.
2. Am Gerät: MULTI ENGINE auf **USR**, mit TYPE die Unit wählen.
3. VCO1/VCO2 runter, MULTI hoch, um nur den eigenen Oszillator zu hören.
4. SHAPE bzw. SHIFT+SHAPE drehen, Edit-Parameter im Edit-Modus unter MULTI ENGINE.

Claude kann nur den Build prüfen, nicht die Hardware. Nach einer Änderung immer sagen, was am Gerät zu testen ist.

## Referenzprojekt: Triangle / „nestri“

Erster funktionierender Oszillator des Nutzers (auf dem minilogue xd getestet):
- Datei `triangle.mnlgxdunit`, Anzeigename „nestri“, keine Edit-Parameter.
- SHAPE: blendet von glattem Dreieck (0) zu gestuftem NES-artigem Dreieck (max), LFO-modulierbar.
- SHIFT+SHAPE: Stufenzahl links 2, Mitte 16 (NES), rechts 128 (`levels = powf(2, 1 + shift * 6)`).
- Kern pro Sample:
  ```cpp
  const float smooth    = (phase < 0.5f) ? (4.f * phase - 1.f) : (3.f - 4.f * phase);
  const float quantized = clip1f(roundf(smooth / step) * step);
  const float sig       = smooth + shape * (quantized - smooth);
  ```
  Verbesserungsidee: `roundf` in der Schleife durch `si_roundf` ersetzen und `1.f / step` vorab berechnen.

## Referenzprojekt: snes-delay

Erster funktionierender Delay-Effekt des Nutzers (auf dem minilogue xd zu testen):
- Datei `snes-delay.mnlgxdunit`, Anzeigename „spc dly", Typ `delfx`.
- Nachbau des SNES SPC700 S-DSP Echo-Hardwareblocks.
- TIME: Delay-Länge 16ms…480ms (wie EDL × 16ms auf dem SNES).
- DEPTH: Wet/Dry-Mix (wie EVOL).
- SHIFT+DEPTH: Echo-Feedback / Reverb-Abkling (wie EFB). Stabil bei 0..1 wegen FIR-Gain < 1.
- FIR-Filter: 8-Tap, fest verdrahtet mit EarthBound/Doom-Koeffizienten
  `FA 0A 1A 13 13 1A 0A FA` (signed, /128) → leichter Tiefpass-Charakter, Summen-Gain ≈ 0,77.
- Ringpuffer L+R je 23040 Floats im SDRAM (~180 KB gesamt), FIR-History (8 × 2 Floats) im SRAM.
- Kern pro Frame:
  ```cpp
  const float rd = s_buf[s_pos];          // ältestes Sample lesen
  fir_push(s_fir, rd);                    // History verschieben
  const float filt = fir_apply(s_fir);   // 8-Tap-FIR
  s_buf[s_pos] = clip1m1f(in + fb*filt); // Feedback zurückschreiben
  if (++s_pos >= delay) s_pos = 0;
  x[0] = clip1m1f(in + depth * filt);    // Ausgang
  ```

## SDK v2 (andere Geräte, nur zur Orientierung)

NTS-1 mkII, NTS-3 kaoss pad, microKORG2 und drumlogue nutzen **v2**: Projekt aus `config.mk`, `header.c`
(Metadaten + bis zu 10 Parameter als `unit_header_t`) und `unit.cc` statt `manifest.json`/`project.mk`.
v1-Code läuft dort nicht ohne Portierung. prologue und NTS-1 (mkI) sind v1 und mit dem minilogue xd binärkompatibel
(laut SDK-README bei gleicher SDK-Version; am saubersten das Projekt für die Zielplattform neu bauen,
Endungen `.prlgunit` bzw. `.ntkdigunit`).
