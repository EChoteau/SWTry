# Game data (`data/game/`)

Layout: see `data/README.md`. File paths are listed in
`include/swcore/data_files.hpp`.

Mapping source and extracted tables (`runes/`, `craft/`, `artifacts/`,
`monsters/monster_attributes.json`, `monsters/monster_archetypes.json`):

- `docs/sources/swe_exporter_mapping.js` is the original SW Exporter mapping
  supplied by the user. It is preserved unchanged as the provenance reference.
- Each `*.json` file is one static table extracted from that source. Numeric IDs
  remain JSON object keys. Only the tables read by the code are kept; any other
  table can be re-extracted from the source when needed.
- The source contains no independent rune-slot table; slot numbers are therefore
  kept as raw values 1 through 6 and are not assigned invented names.
- Artifact attribute `0` in exports is an unset sentinel. It is not present in
  the SW Exporter attribute table and is not assigned a name.
- The source file supplied in this repository did not contain a license header.
  Its upstream license must be verified before redistributing it; no license is
  inferred here.

The C++ code reads the generated JSON tables and does not execute or depend on
the JavaScript source. The separate table files are the source of truth.

Corrections to the extracted tables (deviations from the SWEX source):

- `runes/rune_mainstat.json`: 4* flat ATK (stat 3) and flat DEF (stat 5) max (+15)
  are 112, not 113 as in the SWEX source. Confirmed by the user and by the
  4* +15 runes of the Qui-Gon test export.
- Ancient runes (class 11-16) have the same main stat max (+15) as normal
  runes of the same star count (class - 10); only their innate and substat
  ranges differ. The solver uses `max[class - 10]` for ancient main stats.

`runes/set_effects.json` is maintained separately because the supplied SW Exporter
mapping has no set-bonus table. Confirmed values are recorded explicitly; all
other entries remain `unknown` or `no_stat` and are never inferred by C++.
Intangible is represented as an empty two-piece set and can fill an incomplete
set activation without creating a stat bonus of its own.

Artifact effect labels are exposed through `data/i18n/en.json`. English is the
fallback locale; the artifact mapping files remain the extracted SW Exporter
source tables and contain no hand-written status metadata.

Hand-maintained files (not from SWEX):

- `monsters/skills_overrides.json`: corrections to SWARFARM skill data.
- `account/account_bonuses.json`, `account/summoner_skills.json`: account
  bonuses and tower levels (see `account/account_bonuses.README.txt`).
- `combat/mechanics.json`: combat formulas and assumptions; unknown values are
  `null` and shown as unverified in the GUI.
