# Format Account et statistiques de monstre

L'export SW Exporter charge les instances depuis `unit_list`. Les champs
`unit_id` et `unit_master_id` sont conserves separement ; ce dernier est la
cle de rapprochement avec `ReferenceDB.com2us_id`.

Dans l'export fourni, un monstre sans runes (`unit_master_id=22901`) expose :

- `atk=637`, egal a `ReferenceDB.max_lvl_attack` ;
- `def=593`, egal a `ReferenceDB.max_lvl_defense` ;
- `spd=102`, egal a `ReferenceDB.speed` ;
- `con=582`, tel que `con * 15 = ReferenceDB.max_lvl_hp` (`8730`).

Cette comparaison documente une observation sur cet export et ne constitue pas
encore une regle generale de conversion. Le modele conserve donc les valeurs
brutes `con`, `atk`, `def` et `spd` sans les reinterpreter automatiquement.
