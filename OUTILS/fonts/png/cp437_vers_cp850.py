#!/usr/bin/env python3
# ==============================================================================
# cp437_vers_cp850.py — Police 16x16 de font1 : de CP437 vers CP850
# ==============================================================================
#
# La police 16x16 du moteur (WY-700a, générée par png2c.py) range ses glyphes
# selon la page de codes CP437. Les macros d'accents de font1.h (EGRAVE,
# AGRAVE...) et les polices 8x8 / 8x16 du moteur suivent, elles, CP850. Ce
# script produit font1/f1_16x16.c en CP850, à partir de la police CP437 :
#
#   1. Un caractère présent dans les deux pages garde son dessin, déplacé
#      à sa place CP850 (ex. É reste en 0x90, ¢ passe de 0x9B à 0xBD).
#   2. Une lettre accentuée absente de CP437 est composée à partir des
#      glyphes de la police elle-même :
#        - majuscules À Á Â Ã, È Ê Ë, Ò Ó Ô Õ, Ù Ú Û : le corps tassé que la
#          police dessine déjà pour Ä, É, Ö, Ü (lignes 4 à 12), surmonté de
#          l'accent de la minuscule correspondante (à, è...), remonté de
#          deux lignes (lignes 0 à 2) et centré sur la lettre ;
#        - Ì Í Î Ï et Ý : I et Y tassés de 11 à 9 lignes (deux lignes
#          répétées du fût ou des branches retirées), même accent ;
#        - ã, õ : â, ô dont l'accent est remplacé par le tilde de ñ ;
#        - ý : ÿ dont le tréma est remplacé par l'accent aigu de á ;
#        - ı : i sans son point ;
#        - ø, Ø : o, O barrés d'un trait oblique de deux pixels.
#   3. Les signes absents de CP437 sont tirés, autant que possible, de la
#      police 16x16 elle-même :
#        - ¶ et § : les glyphes 0x14 et 0x15, qui les dessinent déjà ;
#        - ¦ : la barre | de cette police, déjà dessinée coupée en son
#          milieu (CP437 n'a pas d'autre barre verticale simple) ;
#        - ¯ : le trait de _ remonté en haut de la cellule ;
#        - ´ et ¨ : les accents de á et de ä, seuls ;
#        - ¸ : la cédille de ç, seule ;
#        - trait d'union conditionnel : le tiret - ;
#        - ¹ et ³ : dessinés ici dans le style de ² (même hauteur de 7
#          lignes, mêmes traits doubles, même base) ;
#        - ¾ : ¼ dont le 1 est remplacé par un 3 de même taille.
#      Les autres (× ® © ¤ ð Ð þ Þ ‗) viennent de la police 8x16 CP850 du
#      moteur (font1/f1_8x16.c), élargie deux fois (chaque pixel en deux),
#      descendue d'une ligne (la 8x16 pose ses lettres une ligne plus haut :
#      ligne de base 11 au lieu de 12) et centrée sur l'axe des lettres de
#      la 16x16.
#   Les codes 0x00 à 0x1F et 0x7F (symboles graphiques) sont les mêmes
#   dans les deux pages et restent inchangés.
#
# USAGE (depuis la racine du dépôt)
#   python3 OUTILS/fonts/png/cp437_vers_cp850.py
#       lit   OUTILS/fonts/png/f1_16x16-WY-700a.c   (police CP437, png2c.py)
#             font1/f1_8x16.c                       (police 8x16 CP850)
#       écrit font1/f1_16x16.c                      (police 16x16 CP850)
#   python3 OUTILS/fonts/png/cp437_vers_cp850.py --apercu apercu.png
#       écrit aussi une planche des 256 glyphes (Pillow nécessaire)
#
# Le script lit toujours la police CP437 d'origine : il peut être relancé
# sans risque, le résultat est le même.
#
# Python 3 ; bibliothèque standard seule (Pillow pour --apercu uniquement).
# ==============================================================================

import argparse
import os
import re
import sys

SRC_437 = os.path.join("OUTILS", "fonts", "png", "f1_16x16-WY-700a.c")
SRC_816 = os.path.join("font1", "f1_8x16.c")
DEST    = os.path.join("font1", "f1_16x16.c")

# Symboles des codes de contrôle (communs à CP437 et CP850), pour les
# commentaires, comme ascii_cp850.py
GRAPHIQUES = {
    0: '∅', 1: '☺', 2: '☻', 3: '♥', 4: '♦', 5: '♣', 6: '♠', 7: '•',
    8: '◘', 9: '○', 10: '◙', 11: '♂', 12: '♀', 13: '♪', 14: '♫', 15: '☼',
    16: '►', 17: '◄', 18: '↕', 19: '‼', 20: '¶', 21: '§', 22: '▬', 23: '↨',
    24: '↑', 25: '↓', 26: '→', 27: '←', 28: '∟', 29: '↔', 30: '▲', 31: '▼',
    127: '⌂',
}


# ------------------------------------------------------------------------------
# Lecture des polices
# ------------------------------------------------------------------------------

def lire_16x16(chemin):
    """{code: [16 lignes de 16 bits]} d'un fichier produit par png2c.py."""
    texte = open(chemin, encoding="utf-8").read()
    glyphes = {}
    motif = re.compile(r"((?:r\[\s*\d+\]=0x[0-9A-Fa-f]{4};\s*){16})\s*"
                       r"font1DefineChar16x16\([^,]+,\s*(\d+),\s*r\);")
    for m in motif.finditer(texte):
        lignes = [int(x, 16) for x in re.findall(r"=0x([0-9A-Fa-f]{4})", m.group(1))]
        glyphes[int(m.group(2))] = lignes
    if len(glyphes) != 256:
        sys.exit(f"{chemin} : {len(glyphes)} glyphes lus, 256 attendus")
    return glyphes


def lire_8x16(chemin):
    """{code: [16 octets]} d'un fichier produit par psf2c.py."""
    texte = open(chemin, encoding="utf-8").read()
    glyphes = {}
    motif = re.compile(r"font1DefineChar8x16\([^,]+,\s*(\d+),((?:\s*0x[0-9a-fA-F]{2},?){16})\)")
    for m in motif.finditer(texte):
        glyphes[int(m.group(1))] = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{2})", m.group(2))]
    if len(glyphes) != 256:
        sys.exit(f"{chemin} : {len(glyphes)} glyphes lus, 256 attendus")
    return glyphes


# ------------------------------------------------------------------------------
# Opérations sur les glyphes (listes de 16 entiers, bit 15 = pixel gauche)
# ------------------------------------------------------------------------------

def vide():
    return [0] * 16


def decaler(g, dx):
    """Décale horizontalement (dx > 0 : vers la droite), bits sortis perdus."""
    if dx >= 0:
        return [(r >> dx) & 0xFFFF for r in g]
    return [(r << -dx) & 0xFFFF for r in g]


def bornes_x(lignes):
    """(gauche, droite) des colonnes allumées, ou None."""
    tout = 0
    for r in lignes:
        tout |= r
    if not tout:
        return None
    cols = [i for i in range(16) if tout & (0x8000 >> i)]
    return cols[0], cols[-1]


def centre_x(lignes):
    b = bornes_x(lignes)
    return (b[0] + b[1]) / 2.0 if b else 7.5


def accent_de(g, premiere, derniere):
    """Les lignes premiere..derniere d'un glyphe (son accent), seules."""
    return [g[i] if premiere <= i <= derniere else 0 for i in range(16)]


def poser_accent(corps, accent, ligne_haut):
    """Ajoute à corps l'accent (lignes non nulles, gardées dans l'ordre),
    remonté pour commencer à ligne_haut et centré sur le corps."""
    lignes = [r for r in accent if r]
    dx = round(centre_x(corps) - centre_x(lignes))
    lignes = decaler(lignes, dx)
    g = list(corps)
    for i, r in enumerate(lignes):
        if 0 <= ligne_haut + i < 16:
            g[ligne_haut + i] |= r
    return g


def corps_seul(g, premiere, derniere):
    """Les lignes premiere..derniere (le corps de la lettre), le reste vide."""
    return [g[i] if premiere <= i <= derniere else 0 for i in range(16)]


def tasser(g, a_retirer, ligne_bas=12):
    """Retire les lignes données (indices du glyphe) et cale le bas du
    corps sur ligne_bas : 11 lignes deviennent 9."""
    lignes = [g[i] for i in range(16) if g[i] and i not in a_retirer]
    out = vide()
    for i, r in enumerate(lignes):
        out[ligne_bas - len(lignes) + 1 + i] = r
    return out


def barrer(g, haut, bas, gauche, droite):
    """Trait oblique de deux pixels, du bas gauche au haut droit."""
    out = list(g)
    n = bas - haut
    for k in range(n + 1):
        y = bas - k
        x = gauche + round((droite - gauche) * k / n)
        for dx in (0, 1):
            if 0 <= x + dx < 16:
                out[y] |= 0x8000 >> (x + dx)
    return out


def descendre(g, dy):
    """Décale verticalement (dy > 0 : vers le bas), lignes sorties perdues."""
    out = vide()
    for i, r in enumerate(g):
        if 0 <= i + dy < 16:
            out[i + dy] = r
    return out


def bornes_y(g):
    lignes = [i for i, r in enumerate(g) if r]
    return (lignes[0], lignes[-1]) if lignes else None


def centrer_x(g, centre=7.5):
    """Centre l'encre horizontalement sur la colonne centre."""
    return decaler(g, round(centre - centre_x(g)))


def dessin(lignes, haut):
    """Glyphe à partir de lignes de texte ('#' = pixel allumé), la première
    posée à la ligne haut."""
    g = vide()
    for i, txt in enumerate(lignes):
        v = 0
        for x, c in enumerate(txt):
            if c == "#":
                v |= 0x8000 >> x
        g[haut + i] = v
    return g


# ¹ et ³ dans le style de ² (lignes 1 à 7 de la cellule, comme lui)
EXPOSANT_1 = ["....####........",
              "..######........",
              "....####........",
              "....####........",
              "....####........",
              "....####........",
              "..########......"]
EXPOSANT_3 = ["..######........",
              "####..####......",
              "......####......",
              "....######......",
              "......####......",
              "####..####......",
              "..######........"]
# le 3 du numérateur de ¾, de la taille du 1 de ¼ (lignes 2 à 6)
NUMERATEUR_3 = ["######..........",
                "....####........",
                "..####..........",
                "....####........",
                "######.........."]


def doubler_8x16(lignes):
    """Glyphe 8x16 élargi deux fois (chaque pixel en deux), descendu
    d'une ligne (ligne de base de la 16x16) et centré sur l'axe des
    lettres de la 16x16."""
    out = []
    for r in lignes:
        v = 0
        for i in range(8):
            if r & (0x80 >> i):
                v |= 0xC000 >> (2 * i)
        out.append(v)
    return centrer_x(descendre(out, 1))


# ------------------------------------------------------------------------------
# Construction de la police CP850
# ------------------------------------------------------------------------------

def construire(g437, g816):
    def c437(ch):
        """Glyphe CP437 du caractère ch."""
        return g437[ch.encode("cp437")[0]]

    # corps tassés (lignes 4 à 12) que la police dessine déjà
    corpsA = corps_seul(c437("Ä"), 4, 12)
    corpsE = corps_seul(c437("É"), 4, 12)
    corpsO = corps_seul(c437("Ö"), 4, 12)
    corpsU = corps_seul(c437("Ü"), 4, 12)
    # I et Y : 11 lignes (2 à 12) ramenées à 9 en retirant deux lignes
    # répétées (fût du I ; une ligne des branches et une du fût du Y)
    corpsI = tasser(c437("I"), {6, 7})
    corpsY = tasser(c437("Y"), {3, 10})

    # accents des minuscules (lignes 2 à 4), et tilde de ñ (lignes 3 à 4)
    acc = {
        "grave": accent_de(c437("à"), 2, 4),
        "aigu":  accent_de(c437("á"), 2, 4),
        "circ":  accent_de(c437("â"), 2, 4),
        "trema": accent_de(c437("ä"), 2, 4),
        "tilde": accent_de(c437("ñ"), 3, 4),
    }

    majuscules = {
        "À": (corpsA, "grave"), "Á": (corpsA, "aigu"), "Â": (corpsA, "circ"), "Ã": (corpsA, "tilde"),
        "È": (corpsE, "grave"), "Ê": (corpsE, "circ"), "Ë": (corpsE, "trema"),
        "Ì": (corpsI, "grave"), "Í": (corpsI, "aigu"), "Î": (corpsI, "circ"), "Ï": (corpsI, "trema"),
        "Ò": (corpsO, "grave"), "Ó": (corpsO, "aigu"), "Ô": (corpsO, "circ"), "Õ": (corpsO, "tilde"),
        "Ù": (corpsU, "grave"), "Ú": (corpsU, "aigu"), "Û": (corpsU, "circ"),
        "Ý": (corpsY, "aigu"),
    }

    police = {}
    sources = {}
    for code in range(256):
        if code < 32 or code == 127:
            police[code] = g437[code]
            sources[code] = "inchangé"
            continue
        ch = bytes([code]).decode("cp850")

        # 1. présent en CP437 : même dessin
        try:
            police[code] = g437[ch.encode("cp437")[0]]
            sources[code] = "CP437"
            continue
        except UnicodeEncodeError:
            pass

        # 2. lettres composées
        if ch in majuscules:
            corps, nom = majuscules[ch]
            police[code] = poser_accent(corps, acc[nom], 0)
            sources[code] = "composé"
        elif ch in ("ã", "õ"):
            base = c437("â" if ch == "ã" else "ô")
            police[code] = poser_accent(corps_seul(base, 5, 15), acc["tilde"], 3)
            sources[code] = "composé"
        elif ch == "ý":
            police[code] = poser_accent(corps_seul(c437("ÿ"), 5, 15), acc["aigu"], 2)
            sources[code] = "composé"
        elif ch == "ı":
            police[code] = corps_seul(c437("i"), 5, 15)
            sources[code] = "composé"
        elif ch == "ø":
            police[code] = barrer(c437("o"), 5, 13, 2, 13)
            sources[code] = "composé"
        elif ch == "Ø":
            police[code] = barrer(c437("O"), 1, 13, 1, 13)
            sources[code] = "composé"

        # 3. signes tirés de la police 16x16 elle-même
        elif ch in ("¶", "§"):
            police[code] = g437[0x14 if ch == "¶" else 0x15]
            sources[code] = "composé"
        elif ch == "¦":
            police[code] = c437("|")
            sources[code] = "composé"
        elif ch == "¯":
            trait = c437("_")
            police[code] = descendre(trait, 1 - bornes_y(trait)[0])
            sources[code] = "composé"
        elif ch in ("´", "¨"):
            police[code] = centrer_x(acc["aigu" if ch == "´" else "trema"])
            sources[code] = "composé"
        elif ch == "¸":
            police[code] = centrer_x(corps_seul(c437("ç"), 11, 13))   # sous le c
            sources[code] = "composé"
        elif ch == "\xad":
            police[code] = c437("-")
            sources[code] = "composé"
        elif ch in ("¹", "³"):
            police[code] = dessin(EXPOSANT_1 if ch == "¹" else EXPOSANT_3,
                                  bornes_y(c437("²"))[0])
            sources[code] = "composé"
        elif ch == "¾":
            quart = c437("¼")
            haut = bornes_y(quart)[0]
            g = [r & (0x00FF if haut <= i < haut + 5 else 0xFFFF)   # 1 effacé
                 for i, r in enumerate(quart)]
            n3 = dessin(NUMERATEUR_3, haut)
            police[code] = [a | b for a, b in zip(g, n3)]
            sources[code] = "composé"
        else:
            # 4. police 8x16 CP850 élargie
            police[code] = doubler_8x16(g816[code])
            sources[code] = "8x16 x2"
    return police, sources


# ------------------------------------------------------------------------------
# Écriture du .c (même présentation que png2c.py)
# ------------------------------------------------------------------------------

def ecrire_c(police, chemin):
    out = []
    out.append('#include "../font1.h"')
    out.append("")
    out.append("/* =========================================================")
    out.append("   Police : WY-700a.png")
    out.append("   Taille : 16×16 pixels")
    out.append("   Généré par png2c.py (CP437), puis rangé en CP850 par")
    out.append("   OUTILS/fonts/png/cp437_vers_cp850.py (voir son en-tête)")
    out.append("   Encodage commentaires : CP850")
    out.append("   Chaque unsigned int = une ligne de 16 pixels.")
    out.append("   Bit 15 (0x8000) = pixel le plus à gauche.")
    out.append("   ========================================================= */")
    out.append("")
    out.append("void _initFont1_16x16(void)")
    out.append("{")
    out.append("    unsigned int r[16];")
    out.append("")
    for code in range(256):
        g = police[code]
        ch = GRAPHIQUES.get(code, bytes([code]).decode("cp850"))
        if code == 0xF0:
            ch = "-"     # trait d'union conditionnel (invisible sinon)
        out.append(f"    /* --- 0x{code:02X} --- */")
        for k in range(0, 16, 4):
            out.append("    " + " ".join(f"r[{i:2d}]=0x{g[i]:04X};" for i in range(k, k + 4)))
        out.append(f"    font1DefineChar16x16(font1Bank16x16, {code:3d}, r); /* 0x{code:02X} */ /* {ch} */")
        out.append("")
    out.append("}")
    open(chemin, "w", encoding="utf-8", newline="\n").write("\n".join(out) + "\n")


def apercu(police, sources, chemin):
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        sys.exit("--apercu : Pillow est nécessaire (pip install pillow)")
    ech = 3
    img = Image.new("RGB", (16 * 18 * ech, 16 * 18 * ech), (0, 0, 0))
    d = ImageDraw.Draw(img)
    couleurs = {"CP437": (220, 220, 220), "inchangé": (220, 220, 220),
                "composé": (255, 200, 60), "8x16 x2": (90, 200, 255)}
    for code in range(256):
        ox = (code % 16) * 18 * ech
        oy = (code // 16) * 18 * ech
        c = couleurs[sources[code]]
        for y, r in enumerate(police[code]):
            for x in range(16):
                if r & (0x8000 >> x):
                    d.rectangle([ox + x * ech, oy + y * ech, ox + x * ech + ech - 1,
                                 oy + y * ech + ech - 1], fill=c)
    img.save(chemin)


def main():
    ap = argparse.ArgumentParser(description="Police 16x16 de font1 : CP437 -> CP850")
    ap.add_argument("--apercu", metavar="PNG", help="planche des 256 glyphes "
                    "(blanc : repris de CP437, jaune : composé, bleu : 8x16 élargie)")
    a = ap.parse_args()

    police, sources = construire(lire_16x16(SRC_437), lire_8x16(SRC_816))
    ecrire_c(police, DEST)
    nb = {}
    for s in sources.values():
        nb[s] = nb.get(s, 0) + 1
    print(f"{DEST} écrit : " + ", ".join(f"{v} {k}" for k, v in sorted(nb.items())))
    if a.apercu:
        apercu(police, sources, a.apercu)
        print(f"Aperçu : {a.apercu}")


if __name__ == "__main__":
    main()
