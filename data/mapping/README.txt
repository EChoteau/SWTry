Mapping source and generated tables:

- `swe_exporter_mapping.js` is the original SW Exporter mapping supplied by the
  user. It is preserved unchanged as the provenance reference.
- Each `*.json` file is one static table extracted from that source. Numeric IDs
  remain JSON object keys.
- The source file supplied in this repository did not contain a license header.
  Its upstream license must be verified before redistributing it; no license is
  inferred here.

The C++ code reads the generated JSON tables and does not execute or depend on
the JavaScript source. The separate table files are the source of truth.
