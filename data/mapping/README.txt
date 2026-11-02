Mapping source and generated tables:

- `swe_exporter_mapping.js` is the original SW Exporter mapping supplied by the
  user. It is preserved unchanged as the provenance reference.
- Each `*.json` file is one static table extracted from that source. Numeric IDs
  remain JSON object keys.
- The source contains no independent rune-slot table; slot numbers are therefore
  kept as raw values 1 through 6 and are not assigned invented names.
- Artifact attribute `0` in exports is an unset sentinel. It is not present in
  the SW Exporter attribute table and is not assigned a name.
- The source file supplied in this repository did not contain a license header.
  Its upstream license must be verified before redistributing it; no license is
  inferred here.

The C++ code reads the generated JSON tables and does not execute or depend on
the JavaScript source. The separate table files are the source of truth.

`set_effects.json` is maintained separately because the supplied SW Exporter
mapping has no set-bonus table. Confirmed values are recorded explicitly; all
other entries remain `unknown` or `no_stat` and are never inferred by C++.
Intangible is represented as an empty two-piece set and can fill an incomplete
set activation without creating a stat bonus of its own.

Artifact effect labels are exposed through `data/i18n/en.json`. English is the
fallback locale; the artifact mapping files remain the extracted SW Exporter
source tables and contain no hand-written status metadata.
