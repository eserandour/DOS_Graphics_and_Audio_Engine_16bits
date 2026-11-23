/* =========================================================
   FONT1.C — Moteur de rendu texte bitmap
   =========================================================
   API unifiée : font1DrawChar, font1DrawText, font1DrawTextCentered
   acceptent un Font1* qui encapsule indifféremment la police
   BIOS ROM ou une Font1Bank personnelle.

   Les données brutes des glyphes sont dans font1/.
   ========================================================= */

#include <stdlib.h>    /* exit    */
#include <string.h>    /* memset  */
#include <malloc.h>    /* _fmalloc, _ffree */
#include <dos.h>       /* MK_FP  */
#include "video.h"     /* SCREEN_WIDTH */
#include "graphics.h"  /* putPixel */
#include "font1.h"

/* =========================================================
   VARIABLES GLOBALES
   ========================================================= */

/* Pointeur vers la table de glyphes 8x8 en ROM BIOS.
   Adresse fixe F000:FA6E sur tous les PC IBM compatibles.
   far = pointeur 32 bits (segment:offset). */
static unsigned char far *font1Bios = NULL;

/* Font1Bank sous-jacentes, allouées en far heap par font1Init*()(). */
Font1Bank far *font1Bank8x8   = NULL;
Font1Bank far *font1Bank8x16  = NULL;
Font1Bank far *font1Bank16x16  = NULL;

/* Structures Font1 globales prêtes à l'emploi.
   Initialisées par font1InitBios() et font1Init*().
   FONT1_BIOS.bank = NULL : la police vient directement de la ROM,
   pas d'une Font1Bank allouée en mémoire.

   Troisième champ : Font1.size = espacement horizontal en pixels
   entre deux caractères consécutifs dans font1DrawText().
   Pour 8x16, la largeur utile du glyphe est 8 px (pas 16) :
   le pas horizontal est donc 8. */
Font1 FONT1_BIOS       = { FONT1_TYPE_BIOS, NULL,  8, FONT1_CP850 };
Font1 FONT1_BANK_8X8   = { FONT1_TYPE_BANK, NULL,  8, FONT1_CP850 };
Font1 FONT1_BANK_8X16  = { FONT1_TYPE_BANK, NULL,  8, FONT1_CP850 };
Font1 FONT1_BANK_16X16 = { FONT1_TYPE_BANK, NULL, 16, FONT1_CP850 };

/* Majuscules accentuées pleine hauteur (voir font1.h). */
int font1FullCaps = 1;

static void accForget(const Font1Bank far *fb);

/* =========================================================
   INITIALISATION INTERNE
   ========================================================= */

/* Prépare une Font1Bank vide pour la taille donnée.
   Alloue data au juste nécessaire, met la LUT à -1. */
static void _initFont1Bank(Font1Bank far *fb, Font1Size size)
{
    int i, bpg;

    switch (size)
    {
        case FONT1_SIZE_8X8:   bpg = FONT1_BANK_8X8_GLYPH_BYTES;   break;
        case FONT1_SIZE_8X16:  bpg = FONT1_BANK_8X16_GLYPH_BYTES;  break;
        case FONT1_SIZE_16X16: bpg = FONT1_BANK_16X16_GLYPH_BYTES; break;
    }

    fb->size            = size;
    fb->count           = 0;
    fb->capacity        = FONT1_BANK_CAPACITY;
    fb->bytes_per_glyph = bpg;

    fb->data = (unsigned char far *)_fmalloc(
                   (unsigned long)FONT1_BANK_CAPACITY * bpg);
    if (!fb->data) { setVideoMode(0x03); exit(1); }

    for (i = 0; i < 256; i++) fb->lut[i] = -1;
    _fmemset(fb->data, 0, (unsigned long)FONT1_BANK_CAPACITY * bpg);
}

/* =========================================================
   DÉFINITION DE GLYPHES
   ========================================================= */

/* Ajoute ou remplace un glyphe 8x8.
   Alloue un nouveau slot si le caractère n'existe pas encore
   dans la Font1Bank, sinon écrase l'existant. */
void font1DefineChar8x8(Font1Bank far *fb, unsigned char c,
                 unsigned char b0, unsigned char b1,
                 unsigned char b2, unsigned char b3,
                 unsigned char b4, unsigned char b5,
                 unsigned char b6, unsigned char b7)
{
    int slot;
    unsigned char far *g;

    accForget(fb);          /* accents à reprendre */
    if (fb->lut[c] == -1)
    {
        if (fb->count >= fb->capacity) return;
        fb->lut[c] = fb->count++;
    }
    slot = fb->lut[c];
    g    = fb->data + slot * fb->bytes_per_glyph;
    g[0]=b0; g[1]=b1; g[2]=b2; g[3]=b3;
    g[4]=b4; g[5]=b5; g[6]=b6; g[7]=b7;
}

/* Ajoute ou remplace un glyphe 8x16.
   Stockage direct : 16 octets, 1 par ligne, MSB = pixel gauche. */
void font1DefineChar8x16(Font1Bank far *fb, unsigned char c,
                 unsigned char b0,  unsigned char b1,
                 unsigned char b2,  unsigned char b3,
                 unsigned char b4,  unsigned char b5,
                 unsigned char b6,  unsigned char b7,
                 unsigned char b8,  unsigned char b9,
                 unsigned char b10, unsigned char b11,
                 unsigned char b12, unsigned char b13,
                 unsigned char b14, unsigned char b15)
{
    int slot;
    unsigned char far *g;

    accForget(fb);          /* accents à reprendre */
    if (fb->lut[c] == -1)
    {
        if (fb->count >= fb->capacity) return;
        fb->lut[c] = fb->count++;
    }
    slot = fb->lut[c];
    g    = fb->data + slot * fb->bytes_per_glyph;
    g[ 0]=b0;  g[ 1]=b1;  g[ 2]=b2;  g[ 3]=b3;
    g[ 4]=b4;  g[ 5]=b5;  g[ 6]=b6;  g[ 7]=b7;
    g[ 8]=b8;  g[ 9]=b9;  g[10]=b10; g[11]=b11;
    g[12]=b12; g[13]=b13; g[14]=b14; g[15]=b15;
}

/* Ajoute ou remplace un glyphe 16x16.
   Stockage big-endian : octet haut en premier pour que
   le rendu lise les bits de gauche à droite. */
void font1DefineChar16x16(Font1Bank far *fb, unsigned char c,
                  unsigned int rows[16])
{
    int slot, i;
    unsigned char far *g;

    accForget(fb);          /* accents à reprendre */
    if (fb->lut[c] == -1)
    {
        if (fb->count >= fb->capacity) return;
        fb->lut[c] = fb->count++;
    }
    slot = fb->lut[c];
    g    = fb->data + slot * fb->bytes_per_glyph;
    for (i = 0; i < 16; i++)
    {
        g[i*2]   = (unsigned char)(rows[i] >> 8);
        g[i*2+1] = (unsigned char)(rows[i] & 0xFF);
    }
}

/* =========================================================
   RENDU INTERNE
   ========================================================= */

/* Rend un glyphe 8x8.
   Pour chaque ligne, lit l'octet de bits et teste chaque
   bit avec un masque décalé de 0x80 (gauche) à 0x01 (droite).
   far pointer : nécessaire pour la ROM BIOS et par cohérence
   avec les Font1Bank en mémoire far. */
static void _renderGlyph8(int x, int y, unsigned char color,
                          unsigned char far *glyph)
{
    int row, col;
    unsigned char bits;

    /* Glyphe entièrement à l'écran (cas de loin le plus courant) :
       écriture directe, sans l'appel ni le test de bornes de
       putPixel pour chaque pixel ; lignes vides sautées. */
    if (x >= 0 && y >= 0 && x <= SCREEN_WIDTH - 8 && y <= SCREEN_HEIGHT - 8)
    {
        unsigned char far *p = backbuffer + OFFSET(x, y);
        for (row = 0; row < 8; row++, p += SCREEN_WIDTH)
        {
            bits = glyph[row];
            if (!bits) continue;
            if (bits & 0x80) p[0] = color;
            if (bits & 0x40) p[1] = color;
            if (bits & 0x20) p[2] = color;
            if (bits & 0x10) p[3] = color;
            if (bits & 0x08) p[4] = color;
            if (bits & 0x04) p[5] = color;
            if (bits & 0x02) p[6] = color;
            if (bits & 0x01) p[7] = color;
        }
        return;
    }

    for (row = 0; row < 8; row++)
    {
        bits = glyph[row];
        for (col = 0; col < 8; col++)
            if (bits & (0x80 >> col))
                putPixel(x+col, y+row, color);
    }
}

/* Rend un glyphe 8x16.
   Largeur 8 px (1 octet/ligne), hauteur 16 lignes. */
static void _renderGlyph8x16(int x, int y, unsigned char color,
                             unsigned char far *glyph)
{
    int row, col;
    unsigned char bits;

    /* Glyphe entièrement à l'écran : écriture directe (voir
       _renderGlyph8). */
    if (x >= 0 && y >= 0 && x <= SCREEN_WIDTH - 8 && y <= SCREEN_HEIGHT - 16)
    {
        unsigned char far *p = backbuffer + OFFSET(x, y);
        for (row = 0; row < 16; row++, p += SCREEN_WIDTH)
        {
            bits = glyph[row];
            if (!bits) continue;
            if (bits & 0x80) p[0] = color;
            if (bits & 0x40) p[1] = color;
            if (bits & 0x20) p[2] = color;
            if (bits & 0x10) p[3] = color;
            if (bits & 0x08) p[4] = color;
            if (bits & 0x04) p[5] = color;
            if (bits & 0x02) p[6] = color;
            if (bits & 0x01) p[7] = color;
        }
        return;
    }

    for (row = 0; row < 16; row++)
    {
        bits = glyph[row];
        for (col = 0; col < 8; col++)
            if (bits & (0x80 >> col))
                putPixel(x+col, y+row, color);
    }
}

/* Rend un glyphe 16x16.
   Reconstitue chaque ligne en unsigned int depuis 2 octets
   big-endian, puis teste les 16 bits avec masque 0x8000→0x0001. */
static void _renderGlyph16(int x, int y, unsigned char color,
                           unsigned char far *glyph)
{
    int row, col;
    unsigned int bits;

    /* Glyphe entièrement à l'écran : écriture directe (voir
       _renderGlyph8). */
    if (x >= 0 && y >= 0 && x <= SCREEN_WIDTH - 16 && y <= SCREEN_HEIGHT - 16)
    {
        unsigned char far *p = backbuffer + OFFSET(x, y);
        for (row = 0; row < 16; row++, p += SCREEN_WIDTH)
        {
            bits = ((unsigned int)glyph[row*2] << 8) | glyph[row*2+1];
            if (!bits) continue;
            for (col = 0; col < 16; col++)
                if (bits & (0x8000u >> col))
                    p[col] = color;
        }
        return;
    }

    for (row = 0; row < 16; row++)
    {
        bits = ((unsigned int)glyph[row*2] << 8) | glyph[row*2+1];
        for (col = 0; col < 16; col++)
            if (bits & (0x8000 >> col))
                putPixel(x+col, y+row, color);
    }
}

/* =========================================================
   MAJUSCULES ACCENTUÉES PLEINE HAUTEUR
   =========================================================
   Pour chaque banque (et jeu de caractères), on prépare une
   fois, au premier besoin :
   - la forme de chaque accent, découpée dans la minuscule de
     la police (é, è, ê, ë, ã, å) : les lignes d'encre du haut,
     jusqu'à la première ligne vide (sinon, jusqu'au haut de la
     lettre sans accent, e ou a), avec l'écart d'origine ;
   - pour chaque majuscule accentuée : la ligne où commence
     l'accent (au-dessus de la lettre, au même écart que dans
     la minuscule, une ligne vide au moins) et le décalage qui
     centre l'accent sur l'encre de la majuscule.
   Le résultat est rangé dans un petit cache (ACC_CACHE banques,
   environ 150 octets chacune), oublié dès qu'un glyphe de la
   banque change. Le dessin ne fait ensuite que lire des bits.
   ========================================================= */

#define ACC_ACUTE  0
#define ACC_GRAVE  1
#define ACC_CIRC   2
#define ACC_TREMA  3
#define ACC_TILDE  4
#define ACC_RING   5
#define ACC_KINDS  6
#define ACC_ROWS   8          /* hauteur maximale d'un accent */
#define ACC_CACHE  4

/* majuscules : code CP850, code Latin-1, lettre de base, accent */
static const unsigned char accCaps[][4] = {
    { 0xB5, 0xC1, 'A', ACC_ACUTE }, { 0xB7, 0xC0, 'A', ACC_GRAVE },
    { 0xB6, 0xC2, 'A', ACC_CIRC  }, { 0x8E, 0xC4, 'A', ACC_TREMA },
    { 0xC7, 0xC3, 'A', ACC_TILDE }, { 0x8F, 0xC5, 'A', ACC_RING  },
    { 0x90, 0xC9, 'E', ACC_ACUTE }, { 0xD4, 0xC8, 'E', ACC_GRAVE },
    { 0xD2, 0xCA, 'E', ACC_CIRC  }, { 0xD3, 0xCB, 'E', ACC_TREMA },
    { 0xD6, 0xCD, 'I', ACC_ACUTE }, { 0xDE, 0xCC, 'I', ACC_GRAVE },
    { 0xD7, 0xCE, 'I', ACC_CIRC  }, { 0xD8, 0xCF, 'I', ACC_TREMA },
    { 0xA5, 0xD1, 'N', ACC_TILDE },
    { 0xE0, 0xD3, 'O', ACC_ACUTE }, { 0xE3, 0xD2, 'O', ACC_GRAVE },
    { 0xE2, 0xD4, 'O', ACC_CIRC  }, { 0x99, 0xD6, 'O', ACC_TREMA },
    { 0xE5, 0xD5, 'O', ACC_TILDE },
    { 0xE9, 0xDA, 'U', ACC_ACUTE }, { 0xEB, 0xD9, 'U', ACC_GRAVE },
    { 0xEA, 0xDB, 'U', ACC_CIRC  }, { 0x9A, 0xDC, 'U', ACC_TREMA },
    { 0xED, 0xDD, 'Y', ACC_ACUTE }
};
#define ACC_NCAPS  ((int)(sizeof(accCaps) / sizeof(accCaps[0])))

/* minuscule source de chaque accent, et sa lettre sans accent */
static const unsigned char accSrc850[ACC_KINDS]  = { 0x82, 0x8A, 0x88, 0x89, 0xC6, 0x86 };
static const unsigned char accSrcL1 [ACC_KINDS]  = { 0xE9, 0xE8, 0xEA, 0xEB, 0xE3, 0xE5 };
static const unsigned char accSrcBase[ACC_KINDS] = { 'e', 'e', 'e', 'e', 'a', 'a' };

typedef struct {
    const Font1Bank far *bank;   /* NULL : entrée libre          */
    int  charset;
    unsigned int  bits[ACC_KINDS][ACC_ROWS]; /* 16 bits, MSB = colonne 0 */
    unsigned char rows[ACC_KINDS];           /* 0 : pas d'accent         */
    unsigned char ok[ACC_NCAPS];   /* 1 : majuscule redessinée          */
    signed char   top[ACC_NCAPS];  /* première ligne de l'accent        */
    signed char   dx[ACC_NCAPS];   /* décalage horizontal de l'accent   */
} AccInfo;

static AccInfo accCache[ACC_CACHE];
static int     accNext;

/* code -> rang dans accCaps (-1 : pas une majuscule accentuée),
   pour chaque jeu de caractères ; rempli au premier usage */
static signed char accIdx[2][256];
static int         accIdxReady = 0;

static void accForget(const Font1Bank far *fb)
{
    int i;
    for (i = 0; i < ACC_CACHE; i++)
        if (accCache[i].bank == fb) accCache[i].bank = NULL;
}

static int accHeight(const Font1Bank far *fb)
{
    return (fb->size == FONT1_SIZE_8X8) ? 8 : 16;
}

/* ligne r du glyphe c, sur 16 bits (MSB = colonne 0) ; 0 si absent */
static unsigned int accRow(const Font1Bank far *fb, unsigned char c, int r)
{
    const unsigned char far *g;
    if (fb->lut[c] < 0 || r < 0 || r >= accHeight(fb)) return 0;
    g = fb->data + fb->lut[c] * fb->bytes_per_glyph;
    if (fb->size == FONT1_SIZE_16X16)
        return ((unsigned int)g[r * 2] << 8) | g[r * 2 + 1];
    return (unsigned int)g[r] << 8;
}

/* première ligne d'encre (-1 : glyphe vide) ; *cx2 = 2 x centre
   horizontal de l'encre (gauche + droite) */
static int accInk(const Font1Bank far *fb, unsigned char c, int *cx2)
{
    int r, x, top = -1, x0 = 16, x1 = -1, h = accHeight(fb);
    for (r = 0; r < h; r++)
    {
        unsigned int b = accRow(fb, c, r);
        if (!b) continue;
        if (top < 0) top = r;
        for (x = 0; x < 16; x++)
            if (b & (0x8000u >> x)) { if (x < x0) x0 = x; if (x > x1) x1 = x; }
    }
    *cx2 = (x1 >= 0) ? x0 + x1 : 0;
    return top;
}

static AccInfo *accGet(const Font1 *f)
{
    const Font1Bank far *fb;
    const unsigned char *src;
    unsigned char srcGap[ACC_KINDS];
    int srcC2[ACC_KINDS];
    AccInfo *a;
    int i, k, h;

    if (!f || f->type != FONT1_TYPE_BANK || !f->bank || !f->bank->data) return NULL;
    fb = f->bank;

    if (!accIdxReady)
    {
        memset(accIdx, -1, sizeof(accIdx));
        for (i = 0; i < ACC_NCAPS; i++)
        {
            accIdx[0][accCaps[i][0]] = (signed char)i;
            accIdx[1][accCaps[i][1]] = (signed char)i;
        }
        accIdxReady = 1;
    }

    for (i = 0; i < ACC_CACHE; i++)
        if (accCache[i].bank == fb && accCache[i].charset == f->charset)
            return &accCache[i];

    a = &accCache[accNext];
    accNext = (accNext + 1) % ACC_CACHE;
    memset(a, 0, sizeof(*a));
    a->bank    = fb;
    a->charset = f->charset;
    src = (f->charset == FONT1_LATIN1) ? accSrcL1 : accSrc850;
    h   = accHeight(fb);

    /* 1. forme de chaque accent, dans la minuscule */
    for (k = 0; k < ACC_KINDS; k++)
    {
        int r0, r1, gap, bodyTop, dummy, r;
        srcGap[k] = 0;
        srcC2[k]  = 0;
        if (fb->lut[src[k]] < 0 || fb->lut[accSrcBase[k]] < 0) continue;
        r0 = accInk(fb, src[k], &dummy);
        bodyTop = accInk(fb, accSrcBase[k], &srcC2[k]);
        if (r0 < 0 || bodyTop < 0) continue;
        /* lignes d'encre jusqu'à la première ligne vide... */
        for (r1 = r0; r1 < h && accRow(fb, src[k], r1); r1++) ;
        for (gap = 0; r1 + gap < h && !accRow(fb, src[k], r1 + gap); gap++) ;
        if (r1 + gap >= h || r1 > bodyTop)
        {
            /* ...sinon (accent collé à la lettre) : tout ce qui est
               au-dessus du haut de la lettre sans accent */
            r1 = bodyTop;
            gap = 1;
        }
        if (r1 <= r0) continue;
        if (r1 - r0 > ACC_ROWS) r0 = r1 - ACC_ROWS;
        if (gap < 1) gap = 1;
        for (r = r0; r < r1; r++) a->bits[k][r - r0] = accRow(fb, src[k], r);
        a->rows[k] = (unsigned char)(r1 - r0);
        srcGap[k]  = (unsigned char)gap;
    }

    /* 2. place de l'accent sur chaque majuscule */
    for (i = 0; i < ACC_NCAPS; i++)
    {
        unsigned char b = accCaps[i][2];
        int capC2, capTop, d;
        k = accCaps[i][3];
        if (!a->rows[k] || fb->lut[b] < 0) continue;
        capTop = accInk(fb, b, &capC2);
        if (capTop < 0) continue;
        d = capC2 - srcC2[k];                     /* demi-pixels */
        a->ok[i]  = 1;
        a->top[i] = (signed char)(capTop - srcGap[k] - a->rows[k]);
        a->dx[i]  = (signed char)(d >= 0 ? (d + 1) / 2 : -((1 - d) / 2));
    }
    return a;
}

/* rang de c dans accCaps si c est redessinée avec f (sinon -1) */
static int accFind(const Font1 *f, unsigned char c, AccInfo **pa)
{
    AccInfo *a;
    int i;
    if (!font1FullCaps || c < 0x80) return -1;
    a = accGet(f);
    if (!a) return -1;
    i = accIdx[f->charset == FONT1_LATIN1 ? 1 : 0][c];
    if (i < 0 || !a->ok[i]) return -1;
    *pa = a;
    return i;
}

int font1CapAccent(const Font1 *f, unsigned char c, unsigned char *base, int *top)
{
    AccInfo *a;
    int i = accFind(f, c, &a);
    if (i < 0) return 0;
    if (base) *base = accCaps[i][2];
    if (top)  *top  = a->top[i];
    return 1;
}

int font1AccentPixel(const Font1 *f, unsigned char c, int gx, int gy)
{
    AccInfo *a;
    int r, x, k, i = accFind(f, c, &a);
    if (i < 0) return 0;
    k = accCaps[i][3];
    r = gy - a->top[i];
    x = gx - a->dx[i];
    if (r < 0 || r >= a->rows[k] || x < 0 || x >= 16) return 0;
    return (a->bits[k][r] & (0x8000u >> x)) != 0;
}

/* =========================================================
   RENDU PUBLIC — API UNIFIÉE
   ========================================================= */

/* Dessine un caractère avec la police f.
   c est un code direct (0x00–0xFF).
   Pour FONT1_TYPE_BIOS, le code est passé tel quel à la ROM.
   Pour FONT1_TYPE_BANK, le code indexe directement la LUT. */
void font1DrawChar(int x, int y, unsigned char c,
              unsigned char color, Font1 *f)
{
    unsigned char far *glyph;
    unsigned char far *g;

    if (f->type == FONT1_TYPE_BIOS)
    {
        /* Police BIOS : table ROM à F000:FA6E, 128 glyphes (0–127).
           Les codes >= 128 sont hors table : on les remplace par '?'. */
        if (c >= 128) c = '?';
        glyph = font1Bios + ((unsigned int)c * 8);
        _renderGlyph8(x, y, color, glyph);
    }
    else
    {
        /* Police personnelle : chercher le glyphe dans la Font1Bank. */
        int slot;
        AccInfo *a;
        int ai;

        /* majuscule accentuée : lettre pleine hauteur, puis accent */
        ai = accFind(f, c, &a);
        if (ai >= 0)
        {
            int r, k = accCaps[ai][3];
            font1DrawChar(x, y, accCaps[ai][2], color, f);
            for (r = 0; r < a->rows[k]; r++)
            {
                unsigned int b = a->bits[k][r];
                int gx;
                for (gx = 0; b; gx++, b = (b << 1) & 0xFFFFu)
                    if (b & 0x8000u)
                        putPixel(x + a->dx[ai] + gx, y + a->top[ai] + r, color);
            }
            return;
        }

        slot = f->bank->lut[(unsigned int)c];
        if (slot < 0) return;   /* caractère non défini */

        g = f->bank->data + slot * f->bank->bytes_per_glyph;

        switch (f->bank->size)
        {
            case FONT1_SIZE_8X8:
                _renderGlyph8(x, y, color, g);
                break;
            case FONT1_SIZE_8X16:
                _renderGlyph8x16(x, y, color, g);
                break;
            case FONT1_SIZE_16X16:
                _renderGlyph16(x, y, color, g);
                break;
        }
    }
}

/* Dessine une chaîne avec la police f.
   Chaque octet de str est un code direct.
   L'espacement entre caractères = f->size pixels. */
void font1DrawText(int x, int y, const char *str,
              unsigned char color, Font1 *f)
{
    int i    = 0;
    int step = f->size;

    while (str[i] != '\0')
    {
        font1DrawChar(x + i * step, y, (unsigned char)str[i], color, f);
        i++;
    }
}

/* Dessine une chaîne centrée horizontalement.
   Largeur totale = longueur en octets * f->size pixels. */
void font1DrawTextCentered(int y, const char *str,
                      unsigned char color, Font1 *f)
{
    int len = 0, x;
    while (str[len] != '\0') len++;
    x = (SCREEN_WIDTH - len * f->size) / 2;
    if (x < 0) x = 0;
    font1DrawText(x, y, str, color, f);
}

/* =========================================================
   INITIALISATION
   ========================================================= */

/* Charge font1Bios depuis la ROM BIOS.
   MK_FP(segment, offset) construit un far pointer 32 bits.
   F000:FA6E est l'adresse fixe de la table de polices 8x8
   dans le BIOS de tous les PC IBM compatibles. */
void font1InitBios(void)
{
    font1Bios = (unsigned char far *)MK_FP(0xF000, 0xFA6E);
    /* FONT1_BIOS est déjà initialisée statiquement,
       pas besoin de modifier ses champs ici. */
}

/* Alloue font1Bank8x8 en far heap, initialise la Font1Bank,
   charge ses glyphes depuis font1/,
   et met à jour FONT1_BANK_8X8 pour qu'elle pointe dessus. */
void font1InitBank8x8(void)
{
    font1Bank8x8 = (Font1Bank far *)_fmalloc(sizeof(Font1Bank));
    if (!font1Bank8x8) { setVideoMode(0x03); exit(1); }
    _initFont1Bank(font1Bank8x8, FONT1_SIZE_8X8);
    _initFont1_8x8();
    FONT1_BANK_8X8.bank = font1Bank8x8;
}

/* Alloue font1Bank8x16 en far heap, initialise la Font1Bank,
   charge ses glyphes depuis font1/,
   et met a jour FONT1_BANK_8X16 pour qu'elle pointe dessus. */
void font1InitBank8x16(void)
{
    font1Bank8x16 = (Font1Bank far *)_fmalloc(sizeof(Font1Bank));
    if (!font1Bank8x16) { setVideoMode(0x03); exit(1); }
    _initFont1Bank(font1Bank8x16, FONT1_SIZE_8X16);
    _initFont1_8x16();
    FONT1_BANK_8X16.bank = font1Bank8x16;
}

/* Alloue font1Bank16x16 en far heap, initialise la Font1Bank,
   charge ses glyphes depuis font1/,
   et met à jour FONT1_BANK_16X16 pour qu'elle pointe dessus. */
void font1InitBank16x16(void)
{
    font1Bank16x16 = (Font1Bank far *)_fmalloc(sizeof(Font1Bank));
    if (!font1Bank16x16) { setVideoMode(0x03); exit(1); }
    _initFont1Bank(font1Bank16x16, FONT1_SIZE_16X16);
    _initFont1_16x16();
    FONT1_BANK_16X16.bank = font1Bank16x16;
}

/* Libère les deux allocations far d'une Font1Bank.
   Appel sans effet si *fb == NULL.

   *fb est forcément l'une des trois Font1Bank globales
   (font1Bank8x8, font1Bank8x16 ou font1Bank16x16) : on met
   aussi à jour le champ .bank du Font1 correspondant
   (FONT1_BANK_8X8 / _8X16 / _16X16), pour qu'il ne pointe
   plus jamais vers la mémoire qu'on vient de libérer. */
void font1FreeBank(Font1Bank far **fb)
{
    if (!*fb) return;

    accForget(*fb);
    if      (*fb == font1Bank8x8)   FONT1_BANK_8X8.bank   = NULL;
    else if (*fb == font1Bank8x16)  FONT1_BANK_8X16.bank  = NULL;
    else if (*fb == font1Bank16x16) FONT1_BANK_16X16.bank = NULL;

    if ((*fb)->data) { _ffree((*fb)->data); (*fb)->data = NULL; }
    _ffree(*fb);
    *fb = NULL;
}
