#!/usr/bin/env python3
"""
Configure le projet MicroContact (créé par duplicate.py depuis TemplateProject)
en instrument VST3 / CLAP / AU.

À lancer depuis la racine du dépôt iPlug2OOS, après avoir copié les fichiers :
    python3 MicroContact/setup_microcontact.py

Le script est idempotent : on peut le relancer sans risque.
"""
import pathlib
import re
import sys

PROJECT = pathlib.Path(__file__).resolve().parent
CONFIG = PROJECT / "config.h"

REQUIRED = [
    "MicroContact.h",
    "MicroContact.cpp",
    "MicroContact_DSP.h",
    "MicroContact_Params.h",
    "MicroContact_UI.h",
    "config.h",
]

# Valeurs à imposer dans config.h
SETTINGS = {
    "PLUG_NAME": '"MicroContact"',
    "PLUG_MFR": '"Abrasion"',
    "PLUG_TYPE": "1",                   # 1 = instrument
    "PLUG_DOES_MIDI_IN": "1",
    "PLUG_DOES_MIDI_OUT": "0",
    "PLUG_CHANNEL_IO": '"0-2"',         # pas d'entrée audio, sortie stéréo
    "PLUG_WIDTH": "1100",
    "PLUG_HEIGHT": "860",
    "PLUG_FPS": "60",
    "PLUG_UNIQUE_ID": "'McCt'",
    "PLUG_MFR_ID": "'Abrs'",
    "VST3_SUBCATEGORY": '"Instrument|Synth"',
    "AAX_PLUG_CATEGORY_STR": '"Synth"',
    "CLAP_FEATURES": '"instrument"',
}


def fail(msg: str) -> None:
    print(f"ERREUR : {msg}")
    sys.exit(1)


def check_files() -> None:
    missing = [f for f in REQUIRED if not (PROJECT / f).exists()]
    if missing:
        fail(
            "fichiers manquants dans " + str(PROJECT) + " : " + ", ".join(missing) +
            "\nAs-tu bien lancé duplicate.py puis copié les fichiers Micro-Contact dans ce dossier ?"
        )


def patch_config() -> None:
    text = CONFIG.read_text(encoding="utf-8")
    for name, value in SETTINGS.items():
        pattern = re.compile(rf"^(\s*#\s*define\s+{name}\s+).*$", re.MULTILINE)
        if pattern.search(text):
            text = pattern.sub(lambda m: m.group(1) + value, text, count=1)
            print(f"  {name:<24} = {value}")
        else:
            text += f"\n#define {name} {value}\n"
            print(f"  {name:<24} = {value}   (ajouté : absent du modèle)")
    CONFIG.write_text(text, encoding="utf-8")


def patch_au_plists() -> None:
    # Pour l'AU (macOS), le type du composant doit être « aumu » (instrument)
    # et non « aufx » (effet), sinon les hôtes et pluginval le refusent.
    plists = list(PROJECT.glob("resources/*AU*Info.plist"))
    for plist in plists:
        text = plist.read_text(encoding="utf-8")
        patched = re.sub(r"(<key>type</key>\s*<string>)aufx(</string>)", r"\1aumu\2", text)
        if patched != text:
            plist.write_text(patched, encoding="utf-8")
            print(f"  {plist.name} : type AU -> aumu")
    if not plists:
        print("  aucun plist AU trouvé (sans importance pour Windows / FL Studio)")


def main() -> None:
    print("Vérification des fichiers…")
    check_files()
    print("Configuration de config.h :")
    patch_config()
    print("Plists Audio Unit :")
    patch_au_plists()
    print("\nTerminé. Commit puis push : GitHub Actions compile le plugin.")


if __name__ == "__main__":
    main()
