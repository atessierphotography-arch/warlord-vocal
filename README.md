# Warlord Vocal

Compresseur + saturateur pour voix (VST3 + Standalone), construit avec JUCE 8. Mono ou stereo.

## Les 2 modes (boutons SCREAM / CLEAN en haut)
Chaque mode change l'algorithme ET charge ses reglages de depart (tous modifiables ensuite).

**SCREAM : voix agressive**
- Compression en 2 etages : etage principal (coude serre, detection crete) + etage rapide qui ecrase les pics restants
- Saturation dure et asymetrique, drive jusqu'a +30 dB
- Reglages de depart : ratio 8:1, attack 2 ms, release 60 ms, saturation 70%, presence +5 dB, mix parallele 85%

**CLEAN : chant propre**
- Compression 1 etage, coude large (9 dB), detection RMS : plus douce et musicale
- Saturation douce type bande, drive jusqu'a +14 dB
- Reglages de depart : ratio 3.5:1, attack 12 ms, release 120 ms, saturation 25%, presence +3 dB

## Reglages
Input, Threshold, Ratio, Attack, Release, Saturation, Presence (plateau haut a 4 kHz), Mix (parallele), Output.
Rattrapage de gain automatique, filtre de detection a 110 Hz, surechantillonnage x4 sur la saturation,
voyants de reduction de gain.

Interface : panneau metal sombre, gros boutons a stries, selecteur SCREAM / CLEAN avec voyant (rouge / bleu).

## Compiler (Windows, Visual Studio 2022 + CMake)
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
Le VST3 est copie automatiquement dans `C:\Program Files\Common Files\VST3` (lance la compilation en administrateur,
ou ajoute `-DCOPY_AFTER_BUILD=OFF` et copie le fichier a la main).
Dans Reaper : Options > Preferences > Plug-ins > VST > Re-scan.

Sans installer Visual Studio : mets ce dossier sur GitHub, le workflow `.github/workflows/build.yml`
compile le VST3 et le propose en telechargement (onglet Actions > Artifacts).
