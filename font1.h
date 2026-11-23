#ifndef FONT1_H
#define FONT1_H

/* =========================================================
   FONT1.H — Moteur de texte bitmap multi-tailles
   =========================================================
   API unifiée : toutes les fonctions de rendu acceptent
   un pointeur Font1*, quelle que soit la source de la police
   (ROM BIOS ou Font1Bank personnelle).

   Utilisation :
     font1DrawChar(x, y, 'A', 255, &FONT1_BIOS);
     font1DrawChar(x, y, 'A', 255, &FONT1_BANK_8X8);
     font1DrawChar(x, y, 'A', 255, &FONT1_BANK_8X16);
     font1DrawChar(x, y, 'A', 255, &FONT1_BANK_16X16);

   Pour ajouter une nouvelle police :
     1. Déclarer une Font1Bank dans font1/.
     2. La charger avec _initFont1Bank() + font1DefineChar*()().
     3. Créer une Font1 pointant vers cette Font1Bank.

   Fonctionnement d'un glyphe :
   Chaque ligne est encodée en binaire : bit 1 = pixel allumé,
   bit 0 = transparent. Le MSB correspond au pixel le plus
   à gauche de la ligne.
   ========================================================= */

/* ---------------------------------------------------------
   Constantes
   --------------------------------------------------------- */

/* Octets par glyphe selon la taille :
   8x8   :  8 lignes × 1 octet  =   8 octets
   8x16  : 16 lignes × 1 octet  =  16 octets
   16x16 : 16 lignes × 2 octets =  32 octets */
#define FONT1_BANK_8X8_GLYPH_BYTES   8
#define FONT1_BANK_8X16_GLYPH_BYTES  16
#define FONT1_BANK_16X16_GLYPH_BYTES  32

/* ---------------------------------------------------------
   Macros CP850 — accents et caractères spéciaux français
   --------------------------------------------------------- */

/* Usage : font1DrawText(x, y, "caract" eGRAVE "re", 255, &FONT1_BANK_8X8);
   La concaténation de littéraux adjacents est résolue à la
   compilation, sans coût à l'exécution.

   Attention avec \xNN : si le caractère suivant est un chiffre
   hexadécimal (0-9, a-f, A-F), le compilateur l'incorpore dans
   l'échappement. Dans ce cas, couper la chaîne :
     "\x82" "e"  et non  "\x82e"  (lu comme un seul code 0x82E) */

/* Minuscules accentuées (toutes celles de la page de codes CP850) */
#define aAIGU   "\xA0"   /* á   CP850 : 0xA0   Unicode : 0xE1 */
#define aGRAVE  "\x85"   /* à   CP850 : 0x85   Unicode : 0xE0 */
#define aCIRC   "\x83"   /* â   CP850 : 0x83   Unicode : 0xE2 */
#define aTREMA  "\x84"   /* ä   CP850 : 0x84   Unicode : 0xE4 */
#define aTILDE  "\xC6"   /* ã   CP850 : 0xC6   Unicode : 0xE3 */
#define aRING   "\x86"   /* å   CP850 : 0x86   Unicode : 0xE5 */
#define cCED    "\x87"   /* ç   CP850 : 0x87   Unicode : 0xE7 */
#define eAIGU   "\x82"   /* é   CP850 : 0x82   Unicode : 0xE9 */
#define eGRAVE  "\x8A"   /* è   CP850 : 0x8A   Unicode : 0xE8 */
#define eCIRC   "\x88"   /* ê   CP850 : 0x88   Unicode : 0xEA */
#define eTREMA  "\x89"   /* ë   CP850 : 0x89   Unicode : 0xEB */
#define iAIGU   "\xA1"   /* í   CP850 : 0xA1   Unicode : 0xED */
#define iGRAVE  "\x8D"   /* ì   CP850 : 0x8D   Unicode : 0xEC */
#define iCIRC   "\x8C"   /* î   CP850 : 0x8C   Unicode : 0xEE */
#define iTREMA  "\x8B"   /* ï   CP850 : 0x8B   Unicode : 0xEF */
#define nTILDE  "\xA4"   /* ñ   CP850 : 0xA4   Unicode : 0xF1 */
#define oAIGU   "\xA2"   /* ó   CP850 : 0xA2   Unicode : 0xF3 */
#define oGRAVE  "\x95"   /* ò   CP850 : 0x95   Unicode : 0xF2 */
#define oCIRC   "\x93"   /* ô   CP850 : 0x93   Unicode : 0xF4 */
#define oTREMA  "\x94"   /* ö   CP850 : 0x94   Unicode : 0xF6 */
#define oTILDE  "\xE4"   /* õ   CP850 : 0xE4   Unicode : 0xF5 */
#define oBARRE  "\x9B"   /* ø   CP850 : 0x9B   Unicode : 0xF8 */
#define uAIGU   "\xA3"   /* ú   CP850 : 0xA3   Unicode : 0xFA */
#define uGRAVE  "\x97"   /* ù   CP850 : 0x97   Unicode : 0xF9 */
#define uCIRC   "\x96"   /* û   CP850 : 0x96   Unicode : 0xFB */
#define uTREMA  "\x81"   /* ü   CP850 : 0x81   Unicode : 0xFC */
#define yAIGU   "\xEC"   /* ý   CP850 : 0xEC   Unicode : 0xFD */
#define yTREMA  "\x98"   /* ÿ   CP850 : 0x98   Unicode : 0xFF */
/* Majuscules accentuées (toutes celles de la page de codes CP850 ;
   Ÿ, Œ et œ n'existent pas en CP850) */
#define AAIGU   "\xB5"   /* Á   CP850 : 0xB5   Unicode : 0xC1 */
#define AGRAVE  "\xB7"   /* À   CP850 : 0xB7   Unicode : 0xC0 */
#define ACIRC   "\xB6"   /* Â   CP850 : 0xB6   Unicode : 0xC2 */
#define ATREMA  "\x8E"   /* Ä   CP850 : 0x8E   Unicode : 0xC4 */
#define ATILDE  "\xC7"   /* Ã   CP850 : 0xC7   Unicode : 0xC3 */
#define ARING   "\x8F"   /* Å   CP850 : 0x8F   Unicode : 0xC5 */
#define CCED    "\x80"   /* Ç   CP850 : 0x80   Unicode : 0xC7 */
#define EAIGU   "\x90"   /* É   CP850 : 0x90   Unicode : 0xC9 */
#define EGRAVE  "\xD4"   /* È   CP850 : 0xD4   Unicode : 0xC8 */
#define ECIRC   "\xD2"   /* Ê   CP850 : 0xD2   Unicode : 0xCA */
#define ETREMA  "\xD3"   /* Ë   CP850 : 0xD3   Unicode : 0xCB */
#define IAIGU   "\xD6"   /* Í   CP850 : 0xD6   Unicode : 0xCD */
#define IGRAVE  "\xDE"   /* Ì   CP850 : 0xDE   Unicode : 0xCC */
#define ICIRC   "\xD7"   /* Î   CP850 : 0xD7   Unicode : 0xCE */
#define ITREMA  "\xD8"   /* Ï   CP850 : 0xD8   Unicode : 0xCF */
#define NTILDE  "\xA5"   /* Ñ   CP850 : 0xA5   Unicode : 0xD1 */
#define OAIGU   "\xE0"   /* Ó   CP850 : 0xE0   Unicode : 0xD3 */
#define OGRAVE  "\xE3"   /* Ò   CP850 : 0xE3   Unicode : 0xD2 */
#define OCIRC   "\xE2"   /* Ô   CP850 : 0xE2   Unicode : 0xD4 */
#define OTREMA  "\x99"   /* Ö   CP850 : 0x99   Unicode : 0xD6 */
#define OTILDE  "\xE5"   /* Õ   CP850 : 0xE5   Unicode : 0xD5 */
#define OBARRE  "\x9D"   /* Ø   CP850 : 0x9D   Unicode : 0xD8 */
#define UAIGU   "\xE9"   /* Ú   CP850 : 0xE9   Unicode : 0xDA */
#define UGRAVE  "\xEB"   /* Ù   CP850 : 0xEB   Unicode : 0xD9 */
#define UCIRC   "\xEA"   /* Û   CP850 : 0xEA   Unicode : 0xDB */
#define UTREMA  "\x9A"   /* Ü   CP850 : 0x9A   Unicode : 0xDC */
#define YAIGU   "\xED"   /* Ý   CP850 : 0xED   Unicode : 0xDD */

/* Nombre maximum de glyphes par Font1Bank.
   256 = ensemble complet des caractères (0 à 255). */
#define FONT1_BANK_CAPACITY  256

/* ---------------------------------------------------------
   Font1Size — tailles de glyphes supportées
   --------------------------------------------------------- */

/* Font1Size — identifiant de la taille des glyphes.
   N'encode PAS l'espacement horizontal : ce rôle
   est porté exclusivement par Font1.size, renseigné
   explicitement dans font1InitBank*(). */
typedef enum {
    FONT1_SIZE_8X8   = 0,
    FONT1_SIZE_8X16  = 1,
    FONT1_SIZE_16X16 = 2
} Font1Size;

/* ---------------------------------------------------------
   Font1Bank — stockage des glyphes personnels
   --------------------------------------------------------- */

/* Contient tous les glyphes d'une police bitmap personnelle.
   - size            : taille des glyphes (8 ou 16)
   - count           : nombre de glyphes définis
   - capacity        : nombre max de glyphes (256)
   - bytes_per_glyph : taille en octets d'un glyphe
   - lut[256]        : lut[c] = index du glyphe pour le
                       caractère c, ou -1 si non défini
   - data            : pointeur far vers le tableau compact des glyphes.
                       Alloué séparément par _fmalloc(capacity * bytes_per_glyph)
                       pour ne réserver que l'espace utile. */
typedef struct {
    Font1Size         size;
    int               count;
    int               capacity;
    int               bytes_per_glyph;
    int               lut[256];
    unsigned char far *data;   /* alloué au juste nécessaire */
} Font1Bank;

/* ---------------------------------------------------------
   Font1Type — type de source de la police
   --------------------------------------------------------- */

typedef enum {
    FONT1_TYPE_BIOS = 0,   /* police ROM BIOS 8x8            */
    FONT1_TYPE_BANK = 1    /* police personnelle dans une Font1Bank */
} Font1Type;

/* ---------------------------------------------------------
   Font — structure unifiée (BIOS ou Font1Bank)
   --------------------------------------------------------- */

/* Structure passée à toutes les fonctions de rendu.
   type  : indique si la police est BIOS ou personnelle.
   bank  : pointeur far vers la Font1Bank (si FONT1_TYPE_BANK).
           NULL si FONT1_TYPE_BIOS.
   size  : taille en pixels d'un glyphe (espacement).
   charset : FONT1_CP850 pour les polices du moteur (valeur 0,
           donc par défaut) ; FONT1_LATIN1 pour une banque rangée
           en Latin-1 (voir « Majuscules accentuées » plus bas). */
typedef struct {
    Font1Type      type;
    Font1Bank far *bank;
    int            size;
    int            charset;  /* FONT1_CP850 (0, défaut) ou FONT1_LATIN1 :
                                jeu de caractères des glyphes de la banque,
                                pour reconnaître les majuscules accentuées */
} Font1;

/* ---------------------------------------------------------
   Polices globales prêtes à l'emploi
   --------------------------------------------------------- */

/* Police BIOS ROM 8x8 (128 caractères IBM/ASCII). */
extern Font1 FONT1_BIOS;  /*  À éviter : caractères non accentués */

/* Polices personnelles initialisées par font1Init*()(). */
extern Font1 FONT1_BANK_8X8;    /* font1Bank8x8 */
extern Font1 FONT1_BANK_8X16;   /* font1Bank8x16 */
extern Font1 FONT1_BANK_16X16;  /* font1Bank16x16 */

/* Font1Bank sous-jacentes allouées en far heap.
   Initialisées par font1InitBank8x8/16x16(), NULL avant. */
extern Font1Bank far *font1Bank8x8;
extern Font1Bank far *font1Bank8x16;
extern Font1Bank far *font1Bank16x16;

/* ---------------------------------------------------------
   Initialisation
   --------------------------------------------------------- */

/* Charge font1Bios depuis la ROM BIOS (F000:FA6E).
   À appeler avant tout font1DrawChar avec FONT1_BIOS. */
void font1InitBios(void);

/* Initialise font1Bank8x8 et charge ses glyphes depuis font1/.
   Met à jour FONT1_BANK_8X8 pour qu'elle pointe sur font1Bank8x8. */
void font1InitBank8x8(void);

/* Initialise font1Bank8x16 et charge ses glyphes depuis font1/. */
void font1InitBank8x16(void);

/* Initialise font1Bank16x16 et charge ses glyphes. */
void font1InitBank16x16(void);

/* Libère les deux allocations far d'une Font1Bank
   (la struct elle-même et le tableau data).
   Met le pointeur passé à NULL, ainsi que le champ .bank
   du Font1 correspondant (FONT1_BANK_8X8/_8X16/_16X16),
   pour éviter tout pointeur pendant après libération. */
void font1FreeBank(Font1Bank far **fb);

/* ---------------------------------------------------------
   Définition de glyphes
   --------------------------------------------------------- */

/* Ajoute ou remplace un glyphe 8x8 dans une Font1Bank.
   b0 = ligne du haut, b7 = ligne du bas.
   Bit 7 de chaque octet = pixel le plus à gauche. */
void font1DefineChar8x8(Font1Bank far *fb, unsigned char c,
                 unsigned char b0, unsigned char b1,
                 unsigned char b2, unsigned char b3,
                 unsigned char b4, unsigned char b5,
                 unsigned char b6, unsigned char b7);

/* Ajoute ou remplace un glyphe 8x16.
   b0 = ligne du haut, b15 = ligne du bas.
   Bit 7 de chaque octet = pixel le plus a gauche. */
void font1DefineChar8x16(Font1Bank far *fb, unsigned char c,
                 unsigned char b0,  unsigned char b1,
                 unsigned char b2,  unsigned char b3,
                 unsigned char b4,  unsigned char b5,
                 unsigned char b6,  unsigned char b7,
                 unsigned char b8,  unsigned char b9,
                 unsigned char b10, unsigned char b11,
                 unsigned char b12, unsigned char b13,
                 unsigned char b14, unsigned char b15);

/* Ajoute ou remplace un glyphe 16x16.
   rows[16] : 16 unsigned int, bit 15 = pixel gauche. */
void font1DefineChar16x16(Font1Bank far *fb, unsigned char c,
                  unsigned int rows[16]);

/* ---------------------------------------------------------
   Rendu — API unifiée
   --------------------------------------------------------- */

/* Dessine un caractère avec la police f.
   Fonctionne avec FONT1_BIOS, FONT1_BANK_8X8, FONT1_BANK_8X16,
   FONT1_BANK_16X16 ou toute autre Font1 correctement initialisée.
   Borné à l'écran (un glyphe qui dépasse un bord est coupé). */
void font1DrawChar(int x, int y, unsigned char c,
              unsigned char color, Font1 *f);

/* Dessine une chaîne de caractères avec la police f.
   L'espacement entre caractères = f->size pixels. */
void font1DrawText(int x, int y, const char *str,
              unsigned char color, Font1 *f);

/* Dessine une chaîne centrée horizontalement avec la
   police f. */
void font1DrawTextCentered(int y, const char *str,
                      unsigned char color, Font1 *f);


/* ---------------------------------------------------------
   Majuscules accentuées pleine hauteur
   ---------------------------------------------------------
   Dans une cellule de 8 ou 16 lignes, une police doit faire
   tenir l'accent : É, À, Ê... y sont dessinées plus petites
   que E, A (une à deux lignes de moins). Règle du moteur : la
   lettre sous l'accent a toujours la hauteur de la lettre
   seule. font1DrawChar (et donc font1DrawText et
   font1DrawTextCentered) dessine donc chaque majuscule
   accentuée ainsi :
     - la lettre de base, pleine hauteur (le glyphe E pour É) ;
     - l'accent au-dessus, avec le même écart que dans la
       minuscule de la police (au moins une ligne vide), quitte
       à sortir de la cellule par le haut (prévoir la place
       au-dessus d'une première ligne de texte).
   L'accent est pris dans la police elle-même, sur la minuscule
   correspondante (é, è, ê, ë, ã, å) : il garde le dessin, la
   graisse et l'inclinaison de la police, et se centre sur
   l'encre de la majuscule.
   Concerne Á À Â Ä Ã Å É È Ê Ë Í Ì Î Ï Ñ Ó Ò Ô Ö Õ Ú Ù Û Ü Ý
   (Ç ne change pas : sa cédille est dessous). Sans effet sur
   la police BIOS (pas d'accents) ni sur font2 (feuilles ASCII).
   Les polices du moteur sont en CP850. Une banque rangée en
   Latin-1 doit le dire : f.charset = FONT1_LATIN1 (sinon ses
   codes seraient lus en CP850).
   font1FullCaps = 0 rend les glyphes d'origine (tableau de
   glyphes d'une police, par exemple). */
#define FONT1_CP850   0
#define FONT1_LATIN1  1

extern int font1FullCaps;      /* 1 par défaut */

/* c est-il une majuscule accentuée redessinée par le moteur
   avec la police f ? Si oui, rend 1, la lettre de base dans
   *base et, dans *top, la ligne (relative au haut de la
   cellule, souvent négative) où commence l'accent. */
int  font1CapAccent(const Font1 *f, unsigned char c,
                    unsigned char *base, int *top);
/* 1 si le pixel (gx, gy) de la cellule appartient à l'accent
   de la majuscule c (gy peut être négatif : au-dessus de la
   cellule). 0 sinon, ou si c n'est pas concerné. */
int  font1AccentPixel(const Font1 *f, unsigned char c, int gx, int gy);

/* ---------------------------------------------------------
   Chargeurs de glyphes — usage interne uniquement
   Appelées par font1InitBank8x8/8x16/16x16 dans font1.c.
   Ne pas appeler directement depuis les scènes ou main.c.
   --------------------------------------------------------- */
void _initFont1_8x8(void);
void _initFont1_8x16(void);
void _initFont1_16x16(void);

#endif /* FONT1_H */
