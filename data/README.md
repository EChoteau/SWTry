# Données

Règle : **toutes les informations du jeu sont dans des JSON**, jamais en dur
dans le C++. Les chemins de chaque fichier sont centralisés dans
`include/swcore/data_files.hpp` : si un fichier bouge, c'est le seul endroit
à modifier.

```
data/
├── game/                  Données du jeu (versionnées, source de vérité)
│   ├── runes/             Sets, stats, stats principales, qualité, effets de set
│   ├── craft/             Meules et gemmes (plages par rareté)
│   ├── artifacts/         Types, rangs, effets principaux / secondaires
│   ├── monsters/          Éléments, archétypes, corrections des compétences SWARFARM
│   ├── account/           Bonus de compte, tours (compétences d'invocateur)
│   └── combat/            Mécaniques de combat (DEF, ATB, buffs, hypothèses)
├── i18n/                  Libellés affichés (en.json)
├── teams/                 Teams écrites à la main, un sous-dossier par donjon
│   └── giants_keep/       ex. gb12_speed_teshar.json
├── swarfarm/              Généré par swsync (non versionné)
└── user/                  Réglages et sauvegardes du GUI (non versionné)
```

## Ajouter une team

Créer `data/teams/<donjon>/<id>.json` (même format que
`giants_keep/gb12_speed_teshar.json`). Tous les sous-dossiers sont lus ; le
donjon et les étages visés viennent des champs `dungeon_id` et `level_ids`,
le dossier sert seulement au rangement. Plusieurs teams par donjon : un fichier
chacune.

## Ajouter une donnée du jeu

1. Choisir la catégorie dans `data/game/` (ou en créer une).
2. Ajouter le chemin dans `include/swcore/data_files.hpp`.
3. Ne jamais déduire une valeur : inconnue = absente ou `null`, signalée.

Détails et provenance des tables : `data/game/README.md`.
