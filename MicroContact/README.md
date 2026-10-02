# Micro-Contact — Abrasion

Synthétiseur VST3 hybride *Transient-to-Resonance* : un transitoire physique, un corps tonal de précision et une matrice de résonateurs modaux fusionnés en un seul moteur.

## Contenu

| Fichier | Rôle |
|---|---|
| `MicroContact_DSP.h` | Moteur audio complet, en C++ pur (aucune dépendance à iPlug2) |
| `MicroContact_Params.h` | Table des 26 paramètres et conversions moteur ↔ hôte |
| `MicroContact.h` / `.cpp` | Plugin iPlug2 : paramètres, presets, MIDI, interface |
| `MicroContact_UI.h` | Interface vectorielle (logo Abrasion, chemin du signal animé, scope, vumètres) |
| `tests/dsp_smoke_test.cpp` | Banc d'essai du moteur (stabilité, niveaux, < 100 Hz, phase, clics, fs, CPU) |
| `setup_microcontact.py` | Transforme le projet modèle en instrument (config.h, plists AU) |
| `ci/dsp-tests.yml` | Workflow GitHub qui rejoue les tests à chaque push |

## Construire le VST3 avec GitHub (sans rien installer)

Tout se fait dans le navigateur. GitHub compile le plugin pour toi.

1. **Crée ton dépôt.** Va sur <https://github.com/iPlug2/iPlug2OOS>, clique sur **Use this template → Create a new repository**, donne-lui un nom (par exemple `micro-contact`) et crée-le.
2. **Ouvre un Codespace.** Dans ton nouveau dépôt : **Code → Codespaces → Create codespace on master**. Un VS Code s'ouvre dans le navigateur, avec un terminal en bas.
3. **Récupère iPlug2 et crée le projet.** Dans le terminal :
   ```bash
   git submodule update --init --recursive
   ./duplicate.py TemplateProject MicroContact Abrasion
   ```
4. **Ajoute les fichiers Micro-Contact.** Glisse-dépose depuis ton ordinateur vers le dossier `MicroContact/` de l'explorateur de gauche : `MicroContact.h`, `MicroContact.cpp`, `MicroContact_DSP.h`, `MicroContact_Params.h`, `MicroContact_UI.h`, `setup_microcontact.py` et le dossier `tests/`. Accepte le remplacement des deux premiers. Place `ci/dsp-tests.yml` dans `.github/workflows/`.
5. **Configure le projet en instrument :**
   ```bash
   python3 MicroContact/setup_microcontact.py
   ```
6. **Vérifie le nom du projet dans les workflows.** Ouvre les fichiers de `.github/workflows/` et vérifie que `PROJECT_NAME` vaut `MicroContact`. `duplicate.py` le fait normalement, sinon corrige-le à la main.
7. **Envoie le tout sur GitHub :**
   ```bash
   git add -A
   git commit -m "Micro-Contact : moteur, interface et configuration instrument"
   git push
   ```
8. **Récupère le plugin compilé.** Onglet **Actions** du dépôt → la dernière exécution → section **Artifacts** en bas → télécharge l'archive Windows. Elle contient `MicroContact.vst3`.

Chaque `git push` relance la compilation. Si une étape échoue (croix rouge), ouvre-la, copie les dernières lignes du journal et envoie-les moi.

## Installer dans FL Studio (Windows)

1. Décompresse l'archive et copie le dossier `MicroContact.vst3` **en entier** dans :
   `C:\Program Files\Common Files\VST3\`
2. Dans FL Studio : **Options → Manage plugins**, puis **Find installed plugins**. Micro-Contact apparaît dans la liste des *Generators* ; coche l'étoile pour l'ajouter aux favoris.
3. Dans le **Channel Rack**, clique sur **+** et choisis **MicroContact**. Ouvre le **Piano roll** et joue.
4. **Presets** : menu du plugin (flèche en haut à gauche de la fenêtre du plugin) → *808 & Sub*, *Percussions*, *Plucks & Keys*. Les mêmes réglages sont accessibles par les trois boutons de l'en-tête.
5. **Automation** : clic droit sur un bouton → **Create automation clip**. Les 26 paramètres sont automatisables.

### Contrôle MIDI

| Commande | Effet |
|---|---|
| Vélocité | Pression du contact (dosée par *Vélocité*) |
| Pitch bend | ±2 demi-tons |
| Molette de modulation | Ajoute de l'usure (*Erosion / Wear*) |
| Pédale de sustain | Maintien des notes |

### Dépannage

- **Le plugin n'apparaît pas** : vérifie que `C:\Program Files\Common Files\VST3` figure dans les chemins de recherche de *Manage plugins*, puis relance un scan. Si FL le signale comme invalide, installe le *Microsoft Visual C++ Redistributable x64*.
- **Aucun son** : le plugin est un générateur. Il faut lui envoyer des notes (piano roll ou clavier MIDI), pas le placer en effet sur une piste mixer.
- **macOS** : les builds Mac sont aussi produits. Copie `MicroContact.vst3` dans `~/Library/Audio/Plug-Ins/VST3/`. Le plugin n'est pas signé : si macOS le bloque, lance `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/MicroContact.vst3`.

## Lancer les tests en local

```bash
g++ -std=c++17 -O2 -Wall -Wextra MicroContact/tests/dsp_smoke_test.cpp -o dsp_smoke_test && ./dsp_smoke_test
```

## Architecture du moteur

Par voix (8 voix, vol avec fondu de 3 ms) :

- **Excitation procédurale** (arc, roche, verre, clic) : synthétisée à chaque note, pilotée par la vélocité et la hauteur, saturée de façon non linéaire. Aucun échantillon n'est lu.
- **Corps tonal** : deux oscillateurs à tables d'ondes à niveaux de détail (sans repliement). Toutes les formes valent zéro en phase 0, si bien que l'alignement de phase garantit un départ au passage par zéro.
- **Matrice de friction** : 8 modes par voix, excités par le transitoire et par le couplage non linéaire de l'oscillateur. Un passe-haut 24 dB/oct garde le bus « matière » au-dessus de 100 Hz.
- **Space Damping** : cavité FDN ultra-courte, appliquée uniquement au bus matière. Le sub reste sec et mono.

Garanties mesurées par les tests : moins de 0,15 % de l'énergie de la matière sous 100 Hz ; sub dominant à 97 % dans le bas du spectre pour le 808 ; même niveau de 44,1 à 192 kHz (±0,1 dB) ; environ 3,7 % d'un cœur CPU pour 8 voix dans le pire cas.
