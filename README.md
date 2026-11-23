# DOS Graphics & Audio Engine (16 bits)

*Dernière version : 23/11/2026 à 14h54*

Un **moteur graphique et audio** en C89 pur pour **DOS**, compilé avec
**Open Watcom 1.9**, sans aucune dépendance externe au runtime : mode vidéo
**VGA 13h** (320×200, 256 couleurs) piloté directement via les ports,
timer et clavier gérés par interruptions matérielles, musique **S3M** et
effets **WAV** joués sur Sound Blaster par DMA.

Le dépôt inclut neuf **scènes de démonstration** (`scenes/scene0.c` à
`scene8.c`) qui illustrent chacun des modules du moteur : pixels bruts,
palette VGA, polices bitmap, texte par feuille de sprites, scrollers,
rotozoom, primitives 2D, et lecture audio S3M.

> **A graphics and audio engine in C for DOS / Open Watcom 1.9.**
> VGA mode 13h (320×200, 256 colours) and Sound Blaster S3M/WAV playback,
> with nine demo scenes showcasing each module of the engine.

---

## Prérequis

| Outil | Version testée |
|---|---|
| [Open Watcom](https://github.com/open-watcom/open-watcom-v2) | 1.9 |
| DOSBox-X | 2025.02.01 (Linux SDL2 64-bit) |
| Python (outils uniquement) | 3.x |
| Carte **Sound Blaster** (ou compatible) | facultative — voir [Moteur audio](#moteur-audio) |

La compilation se fait **sous DOS** (ou un émulateur DOS tel que
DOSBox-X/VirtualBox) avec `wcc` et `wlink` disponibles dans le `PATH`.

---

## Compilation

```bat
BUILD.BAT
```

Ce script compile chaque fichier `.c` avec `wcc` (modèle mémoire **large**,
cible **8086**, optimisation taille `-os`) puis lie tous les `.obj` via
`wlink @LINK.RSP` pour produire `demo.exe`.

Pour nettoyer les fichiers intermédiaires :

```bat
CLEAN.BAT       :: supprime les .obj, .out et .err
CLEANALL.BAT    :: supprime aussi les .exe
```

---

## Lancement

```bat
demo.exe
```

Appuyez sur **Échap** pour quitter proprement à tout moment (coupe le DMA
audio avant toute autre restauration, pour ne jamais laisser de bruit
résiduel).

`demo.exe` enchaîne les neuf scènes de démonstration ci-dessous et boucle
par défaut (`DEMO_LOOP 1` dans `main.c`) ; mettez cette constante à `0`
pour qu'elle s'arrête après la dernière scène.

---

## Le moteur

Chaque module est indépendant et réutilisable en dehors du contexte des
scènes de démonstration :

| Module | Rôle |
|---|---|
| `video.h/c` | Mode 13h, double buffer en RAM far, `flip()` au retour vertical (image jamais coupée), `waitVRetrace()` |
| `palette.h/c` | Palette DAC VGA 256 couleurs (6 bits/canal) : lecture/écriture (envoyée au DAC au retour vertical, en même temps que l'image), interpolation, cyclage, générateurs procéduraux |
| `graphics.h/c` | Primitives 2D avec clipping intégré : pixel, ligne (Bresenham + Cohen-Sutherland), rectangle, polygone (scanline), cercle (mid-point) |
| `image.h/c` | Chargement one-shot d'images `.raw`/`.pal` (fond d'écran, écran titre) |
| `sprite.h/c` | Sprites préchargés en far heap, blit opaque ou avec transparence (`colorKey`), sans accès disque par frame |
| `font1.h/c` | Moteur de texte bitmap multi-tailles : police ROM BIOS ou banques personnelles (8×8, 8×16, 16×16, toutes en CP850), **majuscules accentuées pleine hauteur** |
| `font2.h/c` | Texte par feuille de sprites `.raw`, décrite par un descripteur (`Font2Desc`) |
| `timer.h/c` | Timer calé sur le retour vertical de l'écran (un tick par image, ~70,086 Hz ; repli : PIT libre à 70 Hz), ISR chaînée au BIOS |
| `keyboard.h/c` | Détection Échap par ISR INT 09h, sans consommer le buffer clavier |
| `sblaster.h/c` | Pilote bas niveau Sound Blaster (DSP + DMA), détection via `BLASTER` |
| `s3m.h/c` | Lecteur de modules **S3M** (Scream Tracker 3), jusqu'à 16 voies mixées en logiciel |
| `wav.h/c` | Mixeur d'effets sonores **WAV**, jusqu'à 8 voies simultanées |
| `audio.h/c` | API audio de haut niveau (`playMusic`, `playSound`, fondus) assemblant `sblaster`/`s3m`/`wav` |
| `scene.h/c` | Gestionnaire de scènes (playlist, callback de fin de scène) utilisé par l'application de démonstration |

---

## Scènes de démonstration

| Scène | Durée | Module illustré |
|---|---|---|
| `scene0` | 1 s | Écran noir (délimiteur de capture vidéo) |
| `scene8` | 32 s | **Moteur audio** : cycle de vie complet d'un module S3M (fade in 6 s → lecture 16 s → fade out 6 s → arrêt 4 s) sur `musique.s3m`, avec affichage en direct de la phase et du statut |
| `scene1` | 6 s | **video/palette** : pixels aléatoires (LCG 32 bits) — fade in 1 s / fade out 3 s |
| `scene2` | 14 s | **palette** : cycle gauche/droite, puis interpolation vers la palette rouge |
| `scene3` | 36 s | **font1** : les quatre polices (BIOS 8×8, Bank 8×8/8×16/16×16), 6 sous-écrans de 6 s |
| `scene4` | 3 s | **font2** : « HELLO » / « WORLD » avec la police sprite 32×32, centrées horizontalement |
| `scene5` | ≈ 39 s | **font2** : scroller horizontal (crédits) puis scroller vertical (générique) ; durée dépendante de la longueur des textes |
| `scene6` | 10 s | **graphics/image** : rotozoom sur `images/freedos.raw` en virgule fixe 16.16, zoom oscillant ×0,6..×2,4 |
| `scene7` | 18 s | **graphics** : toutes les primitives 2D en 6 phases de 3 s (tunnel, spirale, plasma, flocon de Koch, rebonds, composition) |
| `scene0` | 1 s | Écran noir (délimiteur de capture vidéo, seconde occurrence en fin de playlist) |

Durée totale d'un cycle de la playlist par défaut : **≈ 2 min 40 s**, avant
bouclage.

---

## Moteur audio

Huit sons WAV peuvent jouer en même temps (`WAV_MAX_VOICES`, réglable à
la compilation) ; au-delà, le son démarré le plus tôt est remplacé. Sur
une Sound Blaster 16, la sortie est réglée à 22 050 Hz exacts ; sur une
carte plus ancienne (DSP 2.xx ou 3.xx), la carte ne sait produire que
22 222 Hz et le mixage se fait à cette fréquence réelle, pour que la
musique garde sa hauteur et son tempo. Un DSP 1.xx, qui ne sait pas
boucler sans clic, est refusé (silence).

La détection de carte se fait via la variable d'environnement `BLASTER` ;
en son absence, `audioInit()` échoue proprement et `playMusic()`/
`playSound()` deviennent de simples no-op — le reste du moteur (vidéo,
timer, clavier) continue de fonctionner normalement, sans plantage.

---

## Synchronisation avec l'écran

Le timer bat au rythme de l'écran : un tick par retour vertical
(~70,086 Hz en mode 13h, mesuré au démarrage). `flip()` copie l'image
juste après le retour vertical et `setPalette()`/`fadePalette()` font
envoyer la palette au DAC par l'interruption du timer au même instant :
plus d'image sautée ou doublée toutes les 12 secondes (un PIT à 70,002 Hz
décroche lentement de l'écran), plus d'image coupée en deux, et palette
et image changent ensemble. Pendant l'attente du retour vertical, `flip()`
mixe le son (`setFlipIdleHook(audioUpdate)` dans `main.c`) et met le
processeur en sommeil (HLT), ce qui soulage l'émulateur (DOSBox, js-dos).
Si la mesure échoue (pas de VGA), le timer revient au PIT libre à 70 Hz,
comme avant. `installTimer()` doit être appelée après `setVideoMode(0x13)`.

---

## Majuscules accentuées

`font1` dessine É, À, Ê… avec la lettre de base à la même hauteur que la
lettre seule (le glyphe E pour É) et l'accent au-dessus, pris dans la
minuscule de la police (é, è, ê, ë, ã, å) : dans une cellule de 8 ou 16
lignes, les polices dessinent sinon ces majuscules une à deux lignes plus
petites. L'accent peut sortir de la cellule par le haut : laisser un peu
de place au-dessus d'une première ligne de texte. `font1FullCaps = 0`
rend les glyphes d'origine. Une banque rangée en Latin-1 le déclare avec
`maPolice.charset = FONT1_LATIN1`.

---

## Corrections du 23/11/2026

Reprises de la version 32 bits, dans les modules existants :

- **s3m** : `Cxx` va à la ligne xx de l'ordre **suivant** (avant : la
  ligne était prise dans le même motif, qui bouclait indéfiniment — la
  musique de démo ne revenait jamais au début) ; échantillons 16 bits non
  signés et à deux voies lus correctement ; une note sans numéro
  d'instrument garde le volume de la voie ; tous les cas de `Dxx` de
  ST3 (`D0F`/`DF0` ne faisaient rien) et mémoire d'effet partagée ;
  un glissement ne déborde plus sur la ligne suivante ; hauteur des
  notes calculée comme ST3 (les octaves hautes étaient fausses) ;
  `T` ignoré sous 33 ; canaux AdLib ou au-delà de 15 correctement
  répartis sur les voies ; jusqu'à 100 motifs ; motif tronqué lu sans
  déborder.
- **wav** : 8 voies ; le son remplacé quand tout est pris est le plus
  ancien (avant : toujours la voie 0) ; mémoire d'un son rendue dès
  qu'il a fini.
- **audio / sblaster** : codes d'erreur précis pour `playSound()`
  (fichier absent, format) ; fréquence réelle de la carte (voir plus
  haut) ; buffer DMA de 32 768 octets refusé au lieu de déborder.
- **timer / video / palette** : calage sur l'écran (voir plus haut).
- **Interruptions** (timer, clavier, Sound Blaster) : sans contrôle de
  pile — une interruption tombée pendant un appel DOS pouvait arrêter le
  programme sur un faux « débordement de pile » ; `quitRequested` est
  `volatile`.
- **graphics** : intersections du clipping des lignes et du remplissage
  des polygones calculées sur 32 bits (débordement de l'`int` 16 bits
  pour des sommets loin hors de l'écran) ; remplissage au centre des
  pixels (deux polygones voisins se touchent sans recouvrement) ; plus
  d'écriture hors du tableau d'intersections.
- **font1** : police 16×16 rangée en CP850 (elle était en CP437 : ses
  lettres accentuées tombaient sur d'autres codes) ; majuscules
  accentuées pleine hauteur ; dessin direct des glyphes entièrement à
  l'écran (plus rapide) ; toutes les macros CP850 (`AGRAVE`, `ECIRC`…).
- **font2** : glyphes bornés à l'écran ; feuille plus large que
  320 pixels lue sans déborder.
- **sprite / image** : lecture sans débordement de tampon pour une
  largeur supérieure à 320 ; `drawImage` refuse une image qui sortirait
  de l'écran (`IMG_ERR_SIZE`).
- **scene** : numéro de scène hors tableau sans effet ; C89 strict
  (virgule finale d'`enum`, commentaires `//`).

---

## Palettes

Le module `palette.c` fournit une palette dégradée (`grayPalette`) et trois
palettes teintées noir→couleur (`redPalette`, `bluePalette`,
`greenPalette`), en plus d'une palette arc-en-ciel HSV (`rainbowPalette`).
Toutes sont générées procéduralement en mémoire ; les fichiers `.pal`
correspondants dans `images/` (`red.pal`, `blue.pal`, `green.pal`,
`gray.pal`, `rainbow.pal`) sont produits à part par `OUTILS/gen_palettes.py`,
pour l'aperçu et le chargement via `loadPalette()`. `default.pal` (la
palette BIOS d'origine) est générée séparément par `OUTILS/DUMPPAL.C`,
puisqu'elle est lue directement sur le matériel et non calculée par une
fonction du projet. `freedos.pal` (utilisée par `scene6`) est quant à elle
issue d'une conversion d'image via `OUTILS/vgatool.py`, pas d'un
générateur procédural.

---

## Architecture

```
demo.exe
├── main.c            Point d'entrée — playlist de scènes de démonstration
├── app.h             Flag global quitRequested
├── timer.h/c         Timer haute résolution (PIT 8253, 70 Hz, ISR INT 08h)
├── keyboard.h/c      ISR clavier INT 09h — détection Échap
├── video.h/c         Mode 13h, double buffer, flip(), waitVRetrace()
├── palette.h/c       Palette DAC VGA 256 couleurs, lerp, fade, cycle, générateurs
├── graphics.h/c      Primitives 2D clippées (ligne, rect, cercle, polygone)
├── image.h/c         Chargement one-shot d'images RAW + PAL
├── sprite.h/c        Sprites préchargés en far heap, blit opaque/colorKey
│                     (compilé et lié, mais non utilisé par les scènes actuelles)
├── font1.h/c         Polices bitmap multi-tailles (ROM BIOS + fichiers C)
│   └── font1/        Données glyphes : f1_8x8.c, f1_8x16.c (CP850),
│                     f1_16x16.c (police WY-700a, rangée en CP850)
├── font2.h/c         Texte par feuille de sprites .raw
│   └── font2/        Feuilles : font.raw (32×32), 16X16_F2.raw (16×16)
├── sblaster.h/c      Pilote bas niveau Sound Blaster (DSP + DMA)
├── s3m.h/c           Lecteur de modules S3M (Scream Tracker 3)
├── wav.h/c           Mixeur d'effets sonores WAV
├── audio.h/c         API audio de haut niveau (playMusic, playSound, fondus)
│   └── audios/       musique.s3m — module joué par scene8
├── images/           freedos.raw/.pal (scene6) + palettes .pal ci-dessus
├── scene.h/c         Gestionnaire de scènes — callbacks onSceneEnd
└── scenes/           scene0.c … scene8.c — application de démonstration
```

---

## Outils

Le dossier `OUTILS/` contient des utilitaires Python et un outil C
autonome, non nécessaires pour compiler ou exécuter la démo :

| Outil | Rôle |
|---|---|
| `vgatool.py` | Convertit une image en `.raw` + `.pal` + aperçu PNG, ou prévisualise un `.pal` existant |
| `DUMPPAL.C` | Lit la palette BIOS par défaut sur le DAC VGA et écrit `DEFAULT.PAL` |
| `gen_palettes.py` | Découvre automatiquement les générateurs `build<Nom>Palette()` de `palette.c`, les exécute via un mini-programme C natif compilé à la volée, et produit chaque `images/<nom>.pal` + aperçu PNG |
| `s3mcheck.py` | Vérifie qu'un module `.s3m` tient dans le budget mémoire far du moteur audio, en reproduisant les calculs de `s3mLoad()` |
| `fonts/psf2c.py` | Convertit une police PSF1/PSF2 en fichier C de glyphes |
| `fonts/ttf2c.py` | Convertit une police TTF en fichier C de glyphes |
| `fonts/png2c.py` | Convertit un PNG de police bitmap en fichier C de glyphes |
| `fonts/fonteditor.htm` | Éditeur visuel de polices bitmap (HTML/JS, aucune dépendance) |

`DUMPPAL.C` se compile avec Open Watcom, comme le reste du projet ;
`gen_palettes.py`, `s3mcheck.py` et les scripts de `fonts/` s'exécutent
avec Python 3 sur la machine de développement (pas sous DOS).

---

## Licence

Ce projet est distribué sous licence **GNU General Public License v3.0**
(GPL-3.0).
Vous êtes libre de l'utiliser, le modifier et le redistribuer, à condition
que tout travail dérivé soit également publié sous GPL-3.0.

Voir le fichier [`LICENSE`](LICENSE) pour le texte complet.

---

## Version 32 bits

Si les limites du modèle mémoire **16 bits / FAR** deviennent contraignantes, une version **32 bits** du moteur est également disponible :

👉 **[DOS Graphics & Audio Engine – 32 bits](https://github.com/eserandour/DOS_Graphics_and_Audio_Engine_32bits)**

Cette version utilise un **modèle mémoire flat 32 bits** avec **DOS/32A**. Elle permet donc d'adresser un espace mémoire beaucoup plus important que la version 16 bits et d'éviter les contraintes liées aux pointeurs FAR et aux segments de 64 Ko.

Le choix entre les deux versions dépend donc principalement des besoins du projet :

* **16 bits** : modèle mémoire DOS classique, pointeurs **FAR**, compatibilité avec l'environnement 8086 et approche très proche du développement DOS historique.
* **32 bits** : mode protégé avec **DOS/32A**, espace d'adressage linéaire 32 bits et possibilité de manipuler beaucoup plus facilement de grandes quantités de données en mémoire.

Les deux versions conservent la même philosophie : **C, DOS, accès direct au matériel VGA/Sound Blaster et absence de dépendances graphiques ou audio modernes**.
