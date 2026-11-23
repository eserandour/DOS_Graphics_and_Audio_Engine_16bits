/* =========================================================
   S3M.C — Chargeur et moteur de lecture de modules S3M
   =========================================================
   Voir s3m.h pour la documentation de l'API et le sous-
   ensemble du format réellement supporté.

   RÉFÉRENCES DU FORMAT (en-tête, table de périodes, formule
   note -> fréquence) : spécification technique Scream
   Tracker 3 (S3M), voir notamment la table de périodes
   standard (octave 4 = référence) et la formule
     freq = 1712 * C2SPD / (16 * (période >> octave))
   ========================================================= */

#include <stdio.h>    /* FILE, fopen, fread, fseek, fclose   */
#include <string.h>   /* _fmemcpy, _fmemset                  */
#include <malloc.h>   /* _fmalloc, _ffree                    */
#include "s3m.h"

#define S3M_MAX_ORDERS       256
#define S3M_MAX_INSTRUMENTS  100
#define S3M_MAX_PATTERNS     100   /* maximum prévu par le format S3M */

/* ---------------------------------------------------------
   Structures internes
   --------------------------------------------------------- */

typedef struct {
    unsigned char far *data;    /* échantillon 8 bits non signé, NULL si absent */
    unsigned long       len;
    unsigned long       loopStart;
    unsigned long       loopEnd;
    int                 loop;
    unsigned char       defVolume;   /* 0-64 */
    unsigned long       c2spd;
} S3mSample;

typedef struct {
    unsigned char far *data;         /* flux packé, NULL si motif vide */
    unsigned int         packedLen;
    unsigned int         rowOffset[64];
} S3mPattern;

typedef struct {
    int           sampleIdx;   /* -1 = aucun échantillon assigné */
    S3mSample far *samplePtr;  /* &samples[sampleIdx], mis en cache au
                                   déclenchement de la note (voir applyCell)
                                   pour éviter l'indexage par sampleIdx
                                   (multiplication far) dans mixChunk() */
    int far       *volTablePtr; /* &chanVolTable[c][0] pour cette voie,
                                    fixé une fois pour toutes (voir
                                    s3mInit/s3mUnload/s3mLoad) : évite de
                                    réadresser chanVolTable[c] à chaque
                                    échantillon dans mixChunk() */
    unsigned long pos;         /* position 16.16 dans l'échantillon */
    unsigned long step;        /* pas 16.16 par échantillon de sortie */
    unsigned char volume;      /* 0-64, volume de la voie */
    unsigned char mixVol;      /* volume*volumeGlobal/64, recalé par tick */
    unsigned char sharedMem;   /* MÉMOIRE UNIQUE de la voie pour les effets
                                   D E F I J K L Q R S (marqués % dans la
                                   spécification ST3) : un paramètre 00
                                   reprend le dernier paramètre non nul
                                   apparu sur la voie, QUEL QUE SOIT l'effet
                                   qui l'a porté (D00 peut donc reprendre
                                   le paramètre d'un E04 précédent, même
                                   si E n'est pas joué par ce moteur). */
    int           volSlideMode; /* 0 aucun ; 1 fin (tick 0 seul) ; 2 à TOUS
                                    les ticks (D0F, DF0) ; 3 aux ticks non
                                    nuls (D0y, Dx0, Dxy) */
    int           volSlideAmt;  /* variation de volume par pas (signée) */
    int           playing;
} S3mChannel;

/* ---------------------------------------------------------
   État du module chargé
   --------------------------------------------------------- */

static S3mSample  samples[S3M_MAX_INSTRUMENTS];
/* 100 motifs x 134 octets : rangés hors du segment de données
   (far), qui est limité à 64 Ko en modèle large. */
static S3mPattern far patterns[S3M_MAX_PATTERNS];
static unsigned char orders[S3M_MAX_ORDERS];
/* Voie de mixage de chaque canal du fichier (-1 : canal non
   joué). Seuls les canaux PCM activés (réglage 0-15 dans
   l'en-tête) reçoivent une voie, dans l'ordre, jusqu'à
   S3M_MAX_CHANNELS : un module qui utilise les canaux 16 à 31,
   ou qui contient des canaux AdLib, est joué correctement. */
static int chanVoice[32];
static S3mChannel channels[S3M_MAX_CHANNELS];

/* ---------------------------------------------------------
   Table de conversion volume, par voie : chanVolTable[c][b]
   donne directement la contribution (déjà mise à l'échelle du
   mixVol courant) d'un octet d'échantillon brut b (0-255) pour
   la voie c. Recalculée une fois par TICK (voir doTick), donc
   son coût est amorti sur ~samplesPerTick échantillons — le
   mixage par échantillon (mixChunk, boucle la plus chaude du
   moteur) n'a alors plus qu'une simple lecture de table à
   faire, au lieu d'une multiplication 16 bits par échantillon
   ET par voie (une "mul" coûte plusieurs dizaines de cycles
   sur un 8086, largement plus qu'une lecture indexée). C'est
   la technique classique utilisée par les lecteurs de modules
   de l'époque pour tenir le temps réel sur CPU lent. */
static int chanVolTable[S3M_MAX_CHANNELS][256];

static int numOrders       = 0;
static int numInstruments  = 0;
static int numPatterns     = 0;
static int numChannelsToMix = 0;
static int s3mSignedSamples = 0;
static int s3mPlaying       = 0;
static int s3mLoaded        = 0;

static unsigned int currentOrder, currentRow, currentPattern;
static unsigned int speed, tempo;
static unsigned int tickCounter;
static unsigned int samplesLeftInTick;
static unsigned int samplesPerTick;
static unsigned int globalVolume;
static unsigned int masterVolume = 127; /* Mv (offset 0x33), 0-127, lu dans
                                            l'en-tête — voir s3mLoad. 127 =
                                            valeur par défaut si l'en-tête
                                            n'a jamais été lu (avant tout
                                            chargement). */

static int loopFlag = 0;   /* mis à 1 dans advanceRow() quand la table
                               d'ordres reboucle sur son point de départ ;
                               consommé (et remis à 0) par s3mConsumeLoopFlag()
                               — voir déclaration dans s3m.h. */

/* ---------------------------------------------------------
   Fondu (fade-in / fade-out) — enveloppe multiplicative 0-127
   additionnelle, indépendante de Gv/Mv/Vxx : 127 = aucune
   atténuation due au fondu, 0 = silence total dû au fondu.
   Avance en échantillons de sortie réels (voir doTick), donc
   une durée demandée en millisecondes est fidèle quel que soit
   le tempo/vitesse du morceau en cours. Voir s3mFadeTo().
   --------------------------------------------------------- */
static int           fadeLevel            = 127;
static int           fadeStart            = 127;
static int           fadeTarget           = 127;
static unsigned long fadeSamplePos        = 0;
static unsigned long fadeDurationSamples  = 0;   /* 0 = pas de fondu en cours */

static int          posJumpPending, patBreakPending;
static unsigned int  posJumpTarget, patBreakRow;

static unsigned long mixRate = 11025UL;

static int rdErr;

/* ---------------------------------------------------------
   Lecture bas niveau (little-endian, comme tout fichier DOS)
   --------------------------------------------------------- */

static unsigned char rdByte(FILE *f)
{
    unsigned char b;
    if (fread(&b, 1, 1, f) != 1) { rdErr = 1; return 0; }
    return b;
}

static unsigned int rdWord(FILE *f)
{
    unsigned char b[2];
    if (fread(b, 1, 2, f) != 2) { rdErr = 1; return 0; }
    return (unsigned int)b[0] | ((unsigned int)b[1] << 8);
}

static unsigned long rdDword(FILE *f)
{
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4) { rdErr = 1; return 0; }
    return (unsigned long)b[0] | ((unsigned long)b[1] << 8) |
           ((unsigned long)b[2] << 16) | ((unsigned long)b[3] << 24);
}

/* ---------------------------------------------------------
   Chargement des données d'échantillon (PCM 8/16 bits,
   mono/stéréo -> converties en 8 bits non signé mono)
   --------------------------------------------------------- */

#define S3M_CHUNK_FRAMES  128

static int loadSampleData(FILE *f, S3mSample *smp, unsigned long fileOffset,
                           unsigned long srcCount, int bytesPerSample,
                           int channelsIn, int isSigned)
{
    unsigned char nearBuf[S3M_CHUNK_FRAMES * 2];
    unsigned char outChunk[S3M_CHUNK_FRAMES];
    unsigned long destLen;
    unsigned long produced;
    int c;

    destLen = srcCount;
    if (destLen > 65535UL) destLen = 65535UL;

    smp->data = (unsigned char far *)_fmalloc((size_t)destLen);
    if (!smp->data) return S3M_ERR_MEM;

    /* Le moteur est mono. Un échantillon marqué « 2 voies » dans le
       fichier est rangé en DEUX BLOCS successifs de srcCount
       échantillons (gauche, puis droite), non entrelacés : le second
       bloc est moyenné avec le premier, puis oublié. (Une version
       précédente les lisait comme entrelacés : son stéréo faux.) */
    for (c = 0; c < channelsIn; c++)
    {
        fseek(f, (long)(fileOffset + (unsigned long)c * srcCount * (unsigned long)bytesPerSample), SEEK_SET);
        produced = 0;

        while (produced < destLen)
        {
            unsigned int n, k, bytes;

            n = S3M_CHUNK_FRAMES;
            if ((unsigned long)n > destLen - produced)
                n = (unsigned int)(destLen - produced);
            bytes = n * (unsigned int)bytesPerSample;

            if (fread(nearBuf, 1, bytes, f) != bytes)
                break;   /* fichier tronqué : le reste restera silence */

            for (k = 0; k < n; k++)
            {
                unsigned char b;
                int v;

                /* En 16 bits (petit-boutiste), seul l'octet de poids fort
                   compte : le moteur mixe en 8 bits. Signé (format S3M 1) :
                   on le recentre sur 128 ; non signé (format 2, le cas
                   normal) : il est déjà centré sur 128. (Une version
                   précédente lisait tout échantillon 16 bits comme signé :
                   son très distordu pour le cas normal, non signé.) */
                b = (bytesPerSample == 1) ? nearBuf[k] : nearBuf[2 * k + 1];
                v = isSigned ? ((int)(signed char)b + 128) : (int)b;

                if (c == 0)
                    outChunk[k] = (unsigned char)v;
                else
                    outChunk[k] = (unsigned char)(((int)smp->data[produced + k] + v) / 2);
            }
            _fmemcpy(smp->data + produced, outChunk, n);
            produced += n;
        }

        if (c == 0 && produced < destLen)
            _fmemset(smp->data + produced, 128, (size_t)(destLen - produced));
    }

    smp->len = destLen;
    return S3M_OK;
}

/* ---------------------------------------------------------
   Chargement d'un en-tête d'instrument (type PCM uniquement)
   --------------------------------------------------------- */

static int loadInstrument(FILE *f, S3mSample *smp, unsigned long paraPtr)
{
    unsigned long fileOff;
    unsigned char type;
    unsigned char memSegH;
    unsigned int  memSegL;
    unsigned long dataOff;
    unsigned long length, loopBeg, loopEnd;
    unsigned char defVol, packFlag, flags;
    unsigned long c2spd;
    int bytesPerSample, channelsIn;

    smp->data = NULL;
    smp->len  = 0;
    smp->loop = 0;

    if (paraPtr == 0) return S3M_OK;

    fileOff = paraPtr * 16UL;
    fseek(f, (long)fileOff, SEEK_SET);
    type = rdByte(f);

    fseek(f, (long)(fileOff + 0x0D), SEEK_SET);
    memSegH = rdByte(f);
    memSegL = rdWord(f);

    fseek(f, (long)(fileOff + 0x10), SEEK_SET);
    length  = rdDword(f);
    loopBeg = rdDword(f);
    loopEnd = rdDword(f);
    defVol  = rdByte(f);
    rdByte(f);              /* octet réservé (0x1D) */
    packFlag = rdByte(f);
    flags    = rdByte(f);
    c2spd    = rdDword(f);

    if (rdErr) return S3M_ERR_READ;
    if (type != 1)     return S3M_OK;   /* vide ou instrument Adlib : silence */
    if (packFlag != 0)  return S3M_OK;   /* compression non supportée : silence */
    if (length == 0)    return S3M_OK;

    dataOff = (((unsigned long)memSegH << 16) | (unsigned long)memSegL) * 16UL;

    bytesPerSample = (flags & 0x04) ? 2 : 1;
    channelsIn     = (flags & 0x02) ? 2 : 1;

    smp->loop      = (flags & 0x01) ? 1 : 0;
    smp->loopStart = loopBeg;
    smp->loopEnd   = loopEnd;
    smp->defVolume = (defVol > 64) ? 64 : defVol;
    smp->c2spd     = (c2spd == 0) ? 8363UL : c2spd;

    return loadSampleData(f, smp, dataOff, length, bytesPerSample,
                           channelsIn, s3mSignedSamples);
}

/* ---------------------------------------------------------
   Chargement d'un motif (pattern) packé + table des lignes
   --------------------------------------------------------- */

static int loadPattern(FILE *f, S3mPattern far *pat, unsigned long paraPtr)
{
    unsigned int packedLen;
    unsigned char nearBuf[256];
    unsigned int remaining, produced, row, i;
    unsigned char what;

    pat->data = NULL;
    pat->packedLen = 0;
    for (i = 0; i < 64; i++) pat->rowOffset[i] = 0;

    if (paraPtr == 0) return S3M_OK;

    fseek(f, (long)(paraPtr * 16UL), SEEK_SET);
    packedLen = rdWord(f);
    if (rdErr) return S3M_ERR_READ;
    if (packedLen == 0) return S3M_OK;

    /* 8 octets de marge à zéro derrière le flux : une cellule tronquée en
       fin de motif (fichier abîmé) peut annoncer jusqu'à 5 octets de plus
       que ce qui reste ; parseRow() les lit alors dans la marge au lieu
       de déborder du bloc. */
    if (packedLen > 65535U - 8U) return S3M_ERR_FORMAT;
    pat->data = (unsigned char far *)_fmalloc((size_t)packedLen + 8U);
    if (!pat->data) return S3M_ERR_MEM;
    _fmemset(pat->data + packedLen, 0, 8);
    pat->packedLen = packedLen;

    remaining = packedLen;
    produced  = 0;
    while (remaining > 0)
    {
        unsigned int chunk;
        unsigned int got;
        chunk = (remaining > sizeof(nearBuf)) ? (unsigned int)sizeof(nearBuf) : remaining;
        got = (unsigned int)fread(nearBuf, 1, chunk, f);
        if (got != chunk)
        {
            /* Fichier tronqué (ou motif en toute fin de fichier : la
               longueur annoncée compte ses 2 propres octets) : on garde
               ce qui a été lu, le reste est mis à zéro (fin de ligne). */
            _fmemcpy(pat->data + produced, nearBuf, got);
            _fmemset(pat->data + produced + got, 0, (size_t)(packedLen - produced - got));
            break;
        }
        _fmemcpy(pat->data + produced, nearBuf, chunk);
        produced  += chunk;
        remaining -= chunk;
    }

    /* Un seul passage sur le flux packé pour retrouver le début
       de chacune des 64 lignes (voir s3m.h). */
    row = 0;
    i = 0;
    pat->rowOffset[0] = 0;
    while (row < 63 && i < pat->packedLen)
    {
        what = pat->data[i];
        if (what == 0)
        {
            i++;
            row++;
            pat->rowOffset[row] = i;
            continue;
        }
        i++;
        if (what & 0x20) i += 2;
        if (what & 0x40) i += 1;
        if (what & 0x80) i += 2;
    }
    while (row < 63) { row++; pat->rowOffset[row] = pat->packedLen; }

    return S3M_OK;
}

/* ---------------------------------------------------------
   Libération
   --------------------------------------------------------- */

void s3mUnload(void)
{
    int i;

    s3mPlaying = 0;
    s3mLoaded  = 0;
    loopFlag   = 0;

    /* Nouveau module = pas de fondu hérité de l'ancien : sans ce reset,
       un stopMusic()/playMusic() après un fadeMusicOut() referait
       repartir la nouvelle musique en silence (fadeLevel resté à 0). */
    fadeLevel             = 127;
    fadeStart             = 127;
    fadeTarget            = 127;
    fadeSamplePos         = 0;
    fadeDurationSamples   = 0;

    for (i = 0; i < numInstruments; i++)
    {
        if (samples[i].data) { _ffree(samples[i].data); samples[i].data = NULL; }
    }
    for (i = 0; i < numPatterns; i++)
    {
        if (patterns[i].data) { _ffree(patterns[i].data); patterns[i].data = NULL; }
    }
    for (i = 0; i < S3M_MAX_CHANNELS; i++)
    {
        channels[i].playing   = 0;
        channels[i].sampleIdx = -1;
        channels[i].samplePtr = NULL;
        channels[i].pos       = 0;
        channels[i].step      = 0;
        channels[i].volume    = 0;
        channels[i].mixVol    = 0;
        channels[i].sharedMem    = 0;
        channels[i].volSlideMode = 0;
        channels[i].volSlideAmt  = 0;
    }

    numOrders = 0;
    numInstruments = 0;
    numPatterns = 0;
    numChannelsToMix = 0;
}

/* ---------------------------------------------------------
   Chargement complet d'un module
   --------------------------------------------------------- */

void s3mInit(unsigned long rate)
{
    int i;
    mixRate = (rate == 0) ? 11025UL : rate;
    for (i = 0; i < S3M_MAX_CHANNELS; i++)
    {
        channels[i].sampleIdx   = -1;
        channels[i].samplePtr   = NULL;
        /* &chanVolTable[i][0] ne change jamais pour la voie i : fixé
           une fois pour toutes ici (voir déclaration de S3mChannel). */
        channels[i].volTablePtr = chanVolTable[i];
    }
}

int s3mLoad(const char *filename)
{
    FILE *f;
    unsigned int trueOrdNum, trueInsNum, truePatNum;
    unsigned char magic[4];
    unsigned char chSettings[32];
    static unsigned int insParaPtr[S3M_MAX_INSTRUMENTS];  /* static : pas */
    static unsigned int patParaPtr[S3M_MAX_PATTERNS];     /* sur la pile  */
    unsigned int  ffi;
    unsigned char gv, sp, tp, mv;
    int i, err;

    s3mUnload();

    f = fopen(filename, "rb");
    if (!f) return S3M_ERR_FILE;

    rdErr = 0;

    fseek(f, 0x2C, SEEK_SET);
    if (fread(magic, 1, 4, f) != 4) { fclose(f); return S3M_ERR_READ; }
    if (magic[0] != 'S' || magic[1] != 'C' || magic[2] != 'R' || magic[3] != 'M')
    {
        fclose(f);
        return S3M_ERR_FORMAT;
    }

    fseek(f, 0x20, SEEK_SET);
    trueOrdNum = rdWord(f);
    trueInsNum = rdWord(f);
    truePatNum = rdWord(f);

    fseek(f, 0x2A, SEEK_SET);
    ffi = rdWord(f);
    s3mSignedSamples = (ffi == 1) ? 1 : 0;

    /* Gv (volume global initial), Is (vitesse), It (tempo) : trois
       octets consécutifs à partir de 0x30. Un fichier S3M peut fixer
       Gv à autre chose que le maximum (64) — le lire fidèlement plutôt
       que de forcer 64 en dur. */
    fseek(f, 0x30, SEEK_SET);
    gv = rdByte(f);
    sp = rdByte(f);
    tp = rdByte(f);
    speed = (sp == 0) ? 6   : sp;
    tempo = (tp == 0) ? 125 : tp;

    /* Mv (volume maître, offset 0x33) : bit7 = stéréo (ignoré, moteur
       mono), bits 0-6 = volume maître 0-127 tel que réglé par le
       compositeur dans ST3. Contrairement à Gv (point de départ,
       modifiable en cours de lecture via Vxx), Mv est une atténuation
       fixe voulue pour TOUT le morceau — la lire et l'appliquer est
       ce qui permet au son de sortir "comme indiqué dans le s3m"
       plutôt qu'à un niveau numérique arbitrairement maximal (voir
       application dans doTick, et sbSetMixerVolumeMax dans sblaster.c
       pour le gain analogique matériel, qui reste séparé : Mv règle
       le signal NUMÉRIQUE envoyé à la carte, pas le gain de sortie
       de la carte elle-même). */
    fseek(f, 0x33, SEEK_SET);
    mv = rdByte(f);
    masterVolume = mv & 0x7F;

    fseek(f, 0x42, SEEK_SET);
    if (fread(chSettings, 1, 32, f) != 32) { fclose(f); return S3M_ERR_READ; }

    /* Canaux PCM activés (réglage 0-7 gauche, 8-15 droite ; 16-31 :
       AdLib, non joués ici ; bit 7 : désactivé) : une voie de mixage
       chacun, dans l'ordre. (Avant : le numéro de canal servait de
       voie, si bien qu'un canal au-delà de 15 était muet et qu'un
       canal AdLib occupait une voie.) */
    numChannelsToMix = 0;
    for (i = 0; i < 32; i++)
    {
        chanVoice[i] = -1;
        if (chSettings[i] < 16 && numChannelsToMix < S3M_MAX_CHANNELS)
            chanVoice[i] = numChannelsToMix++;
    }

    if (rdErr) { fclose(f); return S3M_ERR_READ; }

    /* Table d'ordres : toujours à 0x60, longueur = trueOrdNum
       (attention : on ne clampe QUE ce qu'on stocke, pas la
       position de lecture, sous peine de désynchroniser tout
       le reste du fichier). */
    fseek(f, 0x60, SEEK_SET);
    numOrders = 0;
    for (i = 0; i < (int)trueOrdNum; i++)
    {
        unsigned char o = rdByte(f);
        if (i < S3M_MAX_ORDERS) { orders[numOrders] = o; numOrders++; }
    }

    numInstruments = 0;
    for (i = 0; i < (int)trueInsNum; i++)
    {
        unsigned int p = rdWord(f);
        if (i < S3M_MAX_INSTRUMENTS) { insParaPtr[numInstruments] = p; numInstruments++; }
    }

    numPatterns = 0;
    for (i = 0; i < (int)truePatNum; i++)
    {
        unsigned int p = rdWord(f);
        if (i < S3M_MAX_PATTERNS) { patParaPtr[numPatterns] = p; numPatterns++; }
    }

    if (rdErr) { fclose(f); s3mUnload(); return S3M_ERR_READ; }

    err = S3M_OK;
    for (i = 0; i < numInstruments && err == S3M_OK; i++)
        err = loadInstrument(f, &samples[i], (unsigned long)insParaPtr[i]);

    for (i = 0; i < numPatterns && err == S3M_OK; i++)
        err = loadPattern(f, &patterns[i], (unsigned long)patParaPtr[i]);

    fclose(f);

    if (err != S3M_OK) { s3mUnload(); return err; }

    /* État de lecture initial. */
    currentOrder = 0;
    while (currentOrder < (unsigned int)numOrders && orders[currentOrder] == 0xFE)
        currentOrder++;
    if (currentOrder >= (unsigned int)numOrders || (numOrders > 0 && orders[currentOrder] == 0xFF))
    {
        s3mLoaded  = 1;
        s3mPlaying = 0;     /* module sans motif jouable : chargé mais silencieux */
        return S3M_OK;
    }

    currentRow     = 0;
    currentPattern = orders[currentOrder];
    if (currentPattern >= (unsigned int)numPatterns) currentPattern = 0;

    tickCounter       = 0;
    samplesLeftInTick = 0;
    samplesPerTick    = 1;
    globalVolume      = (gv > 64) ? 64 : gv;
    posJumpPending    = 0;
    patBreakPending   = 0;

    for (i = 0; i < S3M_MAX_CHANNELS; i++)
    {
        channels[i].playing   = 0;
        channels[i].sampleIdx = -1;
        channels[i].samplePtr = NULL;
        channels[i].pos       = 0;
        channels[i].step      = 0;
        channels[i].volume    = 0;
        channels[i].mixVol    = 0;
        channels[i].sharedMem    = 0;
        channels[i].volSlideMode = 0;
        channels[i].volSlideAmt  = 0;
    }

    s3mLoaded  = 1;
    s3mPlaying = 1;
    return S3M_OK;
}

/* ---------------------------------------------------------
   Fréquence / pas de lecture d'une note
   --------------------------------------------------------- */

static unsigned long s3mNoteStep(unsigned int semitone, unsigned int octave, unsigned long c2spd)
{
    static const unsigned int periodTable[12] =
        { 1712, 1616, 1524, 1440, 1356, 1280, 1208, 1140, 1076, 1016, 960, 907 };
    unsigned long period;
    unsigned long freq;
    unsigned long step;

    if (c2spd == 0) return 0;
    if (octave > 15) octave = 15;

    /* Période ST3, arrondie comme dans Scream Tracker 3 :
         période = 8363 * ((16 * table[note]) >> octave) / c2spd
       puis  fréquence = 8363 * 1712 / période.
       (Une version précédente décalait la table AVANT de la
       multiplier par 16 : aux octaves hautes, la période perdait
       jusqu'à 4 bits de précision et les notes aiguës étaient
       fausses de plusieurs dixièmes de ton.) */
    period = (16UL * (unsigned long)periodTable[semitone]) >> octave;
    period = (8363UL * period + c2spd / 2UL) / c2spd;
    if (period == 0) return 0;

    freq = (8363UL * 1712UL) / period;
    if (freq == 0) return 0;

    step  = (freq / mixRate) << 16;
    step += ((freq % mixRate) << 16) / mixRate;
    return step;
}

/* ---------------------------------------------------------
   Effets "globaux" (vitesse, tempo, sauts, volume global)
   --------------------------------------------------------- */

static void applyCommand(unsigned char cmd, unsigned char info)
{
    switch (cmd)
    {
    case 1:                              /* A : vitesse */
        if (info > 0) speed = info;
        break;
    case 2:                              /* B : saut de position */
        posJumpPending = 1;
        posJumpTarget  = info;
        break;
    case 3:                              /* C : rupture de motif
                                             Paramètre encodé en BCD dans le
                                             format S3M ("decimal-as-hex") :
                                             l'octet 0x25 désigne la ligne 25,
                                             pas la ligne 37. Si la ligne
                                             décodée dépasse 63, l'effet est
                                             ignoré (pas clampé) — comportement
                                             standard du format. */
        {
            unsigned char row = (unsigned char)(((info >> 4) * 10) + (info & 0x0F));
            if (row <= 63)
            {
                patBreakPending = 1;
                patBreakRow     = row;
            }
        }
        break;
    case 20:                             /* T : tempo */
        if (info >= 33) tempo = info;    /* ST3 ignore T < 33 */
        break;
    case 22:                             /* V : volume global */
        globalVolume = (info > 64) ? 64 : info;
        break;
    default:
        break;   /* effet non supporté : ignoré (voir s3m.h) */
    }
}

/* ---------------------------------------------------------
   Application d'une cellule (une voie, une ligne)
   --------------------------------------------------------- */

static void applyCell(unsigned int column,
                       int hasNote, unsigned char note, unsigned char instr,
                       int hasVol,  unsigned char vol,
                       int hasCmd,  unsigned char cmd, unsigned char info)
{
    S3mChannel *ch = NULL;

    if (chanVoice[column & 31] >= 0) ch = &channels[chanVoice[column & 31]];

    /* Scream Tracker 3 : D, E, F, I, J, K, L, Q, R et S partagent UNE
       SEULE mémoire par voie — le dernier paramètre non nul apparu sur
       la voie, quel que soit l'effet qui le portait. */
    if (hasCmd && ch &&
        (cmd == 4 || cmd == 5 || cmd == 6 || cmd == 9 || cmd == 10 ||
         cmd == 11 || cmd == 12 || cmd == 17 || cmd == 18 || cmd == 19))
    {
        if (info != 0) ch->sharedMem = info;
        else           info = ch->sharedMem;
    }

    if (hasCmd) applyCommand(cmd, info);

    if (!ch) return;

    /* Note 0xFF ("pas de note") avec un numéro d'instrument : c'est un
       instrument SANS note, traité par la branche else ci-dessous. */
    if (hasNote && note == 0xFF) hasNote = 0;

    if (hasNote)
    {
        if (note == 0xFE)
        {
            ch->playing = 0;
        }
        else
        {
            unsigned int octave, semitone;
            int sIdx;

            if (instr != 0 && (int)instr <= numInstruments)
                ch->sampleIdx = (int)instr - 1;
            sIdx = ch->sampleIdx;

            if (sIdx >= 0 && sIdx < numInstruments && samples[sIdx].data)
            {
                octave   = (note >> 4) & 0x0F;
                semitone = note & 0x0F;
                if (semitone > 11) semitone = 11;

                ch->step = s3mNoteStep(semitone, octave, samples[sIdx].c2spd);
                if (ch->step != 0)
                {
                    ch->pos       = 0;
                    ch->playing   = 1;
                    /* Une note SANS numéro d'instrument garde le volume
                       courant de la voie ; seul un numéro d'instrument le
                       remet à la valeur par défaut de l'échantillon. */
                    if (instr != 0) ch->volume = samples[sIdx].defVolume;
                    /* Pointeur mis en cache ici, au déclenchement de la
                       note : mixChunk() n'a alors plus besoin d'indexer
                       samples[] par sampleIdx à chaque échantillon
                       mixé (voir déclaration de S3mChannel). */
                    ch->samplePtr = &samples[sIdx];
                }
            }
        }
    }
    else if (instr != 0 && (int)instr <= numInstruments)
    {
        ch->sampleIdx = (int)instr - 1;
        if (ch->sampleIdx >= 0 && ch->sampleIdx < numInstruments)
            ch->volume = samples[ch->sampleIdx].defVolume;
    }

    if (hasVol && vol <= 64)
        ch->volume = vol;

    /* 4 = 'D', glissement de volume (paramètre déjà résolu par la
       mémoire). Cas de la spécification ST3 :
         DxF (x>0), DFF  : fin, monte de x (15), tick 0 seul
         DFy (y>0)       : fin, descend de y, tick 0 seul
         D0F / DF0       : descend / monte de 15 à TOUS les ticks
         D0y             : descend de y aux ticks non nuls
         Dx0             : monte de x aux ticks non nuls
         Dxy (x,y > 0)   : traité comme D0y (descend de y)
       (Avant : D0F et DF0 ne faisaient rien, et un Dxy montait.) */
    ch->volSlideMode = 0;
    ch->volSlideAmt  = 0;
    if (hasCmd && cmd == 4 && info != 0)
    {
        unsigned int up   = (info >> 4) & 0x0F;
        unsigned int down = info & 0x0F;

        if (down == 0x0F && up != 0)       { ch->volSlideMode = 1; ch->volSlideAmt =  (int)up;   }
        else if (up == 0x0F && down != 0)  { ch->volSlideMode = 1; ch->volSlideAmt = -(int)down; }
        else if (down == 0x0F)             { ch->volSlideMode = 2; ch->volSlideAmt = -15; }
        else if (up == 0x0F)               { ch->volSlideMode = 2; ch->volSlideAmt =  15; }
        else if (down != 0)                { ch->volSlideMode = 3; ch->volSlideAmt = -(int)down; }
        else                               { ch->volSlideMode = 3; ch->volSlideAmt =  (int)up;   }
    }
}

/* ---------------------------------------------------------
   Lecture des cellules d'une ligne de motif
   --------------------------------------------------------- */

static void parseRow(S3mPattern far *pat, unsigned int row)
{
    unsigned int i;
    unsigned char what;

    if (!pat->data) return;
    i = pat->rowOffset[row];

    for (;;)
    {
        unsigned int channel;
        unsigned char note, instr, vol, cmd, info;
        int hasNote, hasVol, hasCmd;

        if (i >= pat->packedLen) break;
        what = pat->data[i++];
        if (what == 0) break;

        channel = what & 0x1F;
        note = 0xFF; instr = 0; vol = 0xFF; cmd = 0; info = 0;
        hasNote = 0; hasVol = 0; hasCmd = 0;

        if (what & 0x20)
        {
            note    = pat->data[i++];
            instr   = pat->data[i++];
            hasNote = 1;
        }
        if (what & 0x40)
        {
            vol    = pat->data[i++];
            hasVol = 1;
        }
        if (what & 0x80)
        {
            cmd    = pat->data[i++];
            info   = pat->data[i++];
            hasCmd = 1;
        }

        applyCell(channel, hasNote, note, instr, hasVol, vol, hasCmd, cmd, info);
    }
}

/* ---------------------------------------------------------
   Avance à la ligne / au motif suivant
   --------------------------------------------------------- */

static void advanceRow(void)
{
    unsigned int nextRow;
    unsigned int nextOrder;

    nextOrder = currentOrder;
    nextRow   = currentRow + 1;

    if (patBreakPending)
    {
        /* Cxx : ligne patBreakRow de l'ordre SUIVANT (ou de l'ordre
           visé par un Bxx posé sur la même ligne, voir ci-dessous).
           (Avant : la ligne était prise dans le MÊME motif, qui
           rebouclait alors indéfiniment sur lui-même.) */
        nextOrder = currentOrder + 1;
        nextRow   = patBreakRow;
    }

    if (posJumpPending)
    {
        nextOrder = posJumpTarget;
        if (!patBreakPending) nextRow = 0;
    }
    else if (!patBreakPending && nextRow >= 64)
    {
        nextOrder = currentOrder + 1;
        nextRow   = 0;
    }

    while (nextOrder < (unsigned int)numOrders && orders[nextOrder] == 0xFE)
        nextOrder++;

    if (nextOrder >= (unsigned int)numOrders ||
        (numOrders > 0 && orders[nextOrder] == 0xFF))
    {
        nextOrder = 0;
        while (nextOrder < (unsigned int)numOrders && orders[nextOrder] == 0xFE)
            nextOrder++;
        nextRow = 0;
        loopFlag = 1;   /* la table d'ordres reboucle sur son point de
                            départ — voir s3mConsumeLoopFlag() */
    }

    if (nextOrder >= (unsigned int)numOrders || numOrders == 0)
    {
        s3mPlaying = 0;      /* aucun motif jouable : silence plutôt que planter */
        return;
    }

    currentOrder   = nextOrder;
    currentRow     = nextRow;
    currentPattern = orders[currentOrder];
    if (currentPattern >= (unsigned int)numPatterns) currentPattern = 0;
}

/* ---------------------------------------------------------
   Glissement de volume Dxx — un pas, pour les voies dont le mode
   s'applique à ce tick : au tick 0 de la ligne, le glissement fin
   (mode 1, une seule fois) et le premier pas de D0F / DF0 (mode 2) ;
   aux ticks suivants, D0F / DF0 (mode 2) et les glissements
   normaux (mode 3).
   --------------------------------------------------------- */
static void applyVolSlide(int firstTick)
{
    unsigned int i;

    for (i = 0; i < (unsigned int)numChannelsToMix; i++)
    {
        S3mChannel *ch = &channels[i];
        int v;

        if (ch->volSlideMode == 0) continue;
        if (firstTick  && ch->volSlideMode == 3) continue;
        if (!firstTick && ch->volSlideMode == 1) continue;

        v = (int)ch->volume + ch->volSlideAmt;
        if (v < 0)  v = 0;
        if (v > 64) v = 64;
        ch->volume = (unsigned char)v;
    }
}

/* ---------------------------------------------------------
   Un "tick" du morceau (une fraction de ligne, cadencée par
   speed/tempo — voir doTick / s3mMix)
   --------------------------------------------------------- */

static void doTick(void)
{
    unsigned int i;

    if (tickCounter == 0)
    {
        posJumpPending  = 0;
        patBreakPending = 0;

        /* Les effets de voie ne valent que pour leur ligne : une voie
           sans cellule sur la nouvelle ligne ne doit pas continuer le
           glissement de la ligne précédente (avant : il continuait). */
        for (i = 0; i < (unsigned int)numChannelsToMix; i++)
        {
            channels[i].volSlideMode = 0;
            channels[i].volSlideAmt  = 0;
        }

        parseRow(&patterns[currentPattern], currentRow);
        if (!s3mPlaying) return;

        /* Les glissements fins (DxF/DFx) s'appliquent une seule fois,
           exactement ici, au déclenchement de la nouvelle ligne —
           jamais sur les ticks suivants ; D0F/DF0 aussi (premier pas). */
        applyVolSlide(1);

        advanceRow();
        tickCounter = speed;
    }
    else
    {
        applyVolSlide(0);
    }

    if (tickCounter > 0) tickCounter--;

    /* Avance du fondu (fade-in/fade-out), en échantillons de sortie
       réels plutôt qu'en ticks : une durée de fondu demandée en
       millisecondes reste donc correcte quel que soit le tempo/vitesse
       du morceau (voir s3mFadeTo). On utilise ici la durée du tick
       PRÉCÉDENT (samplesPerTick n'est recalculée qu'en bas de cette
       fonction) : un décalage d'un tick sur un fondu qui dure
       typiquement plusieurs centaines de ticks est inaudible. */
    if (fadeDurationSamples > 0)
    {
        fadeSamplePos += samplesPerTick;
        if (fadeSamplePos >= fadeDurationSamples)
        {
            fadeLevel = fadeTarget;
            fadeDurationSamples = 0;   /* fondu terminé */
        }
        else
        {
            long diff = (long)fadeTarget - (long)fadeStart;
            fadeLevel = fadeStart +
                (int)((diff * (long)fadeSamplePos) / (long)fadeDurationSamples);
        }
    }

    for (i = 0; i < (unsigned int)numChannelsToMix; i++)
    {
        unsigned int v;
        int mv;
        int combinedMv;

        channels[i].mixVol =
            (unsigned char)(((unsigned int)channels[i].volume * globalVolume) >> 6);

        /* mv (0-64) atténué par masterVolume et fadeLevel (0-127
           chacun), ramené à la MÊME échelle 0-64 que mv d'origine :
           combinedMv=64 quand masterVolume=fadeLevel=127 (aucune
           atténuation), ce qui redonne exactement le calcul d'avant
           l'ajout de Mv/fadeLevel. UNE SEULE division 32 bits ici,
           par voie et par tick (16 au pire) — PAS par octet mixé :
           une première version divisait 256 fois par voie et par
           tick (jusqu'à 4096 divisions 32 bits/tick, très coûteux
           sur 8086/286 sans division matérielle rapide), ce qui
           ralentissait le tick au point de décrocher le mixage en
           temps réel et de produire des saccades audibles pendant
           les fondus. */
        combinedMv = (int)(((long)channels[i].mixVol *
                             (long)masterVolume * (long)fadeLevel) / (127L * 127L));

        /* Table de conversion volume pour cette voie (voir déclaration
           de chanVolTable ci-dessus), recalculée à chaque tick pour
           chaque voie — 256 itérations amorties sur ~samplesPerTick
           échantillons. Boucle strictement identique (une seule
           multiplication 16 bits + décalage par octet, PAS de long,
           PAS de division) à celle d'avant l'ajout de Mv/fadeLevel :
           combinedMv porte déjà toute l'atténuation nécessaire (voir
           ci-dessus). */
        mv = combinedMv;
        for (v = 0; v < 256U; v++)
            chanVolTable[i][v] = (((int)v - 128) * mv) >> 6;
    }

    samplesPerTick = (unsigned int)(((unsigned long)mixRate * 5UL) / ((unsigned long)tempo * 2UL));
    if (samplesPerTick == 0) samplesPerTick = 1;
}

/* ---------------------------------------------------------
   Résolution d'index avec bouclage d'échantillon
   --------------------------------------------------------- */

static unsigned long resolveIndex(S3mSample *sm, unsigned long idx, int *stop)
{
    unsigned long span;

    *stop = 0;
    if (sm->loop && sm->loopEnd > sm->loopStart && sm->loopEnd <= sm->len && idx >= sm->loopEnd)
    {
        span = sm->loopEnd - sm->loopStart;
        idx  = sm->loopStart + ((idx - sm->loopStart) % span);
        return idx;
    }
    if (idx >= sm->len) *stop = 1;
    return idx;
}

/* ---------------------------------------------------------
   Mixage de 'count' échantillons de sortie (toutes voies)
   --------------------------------------------------------- */

static void mixChunk(unsigned char far *dst, unsigned int count)
{
    unsigned int s;

    for (s = 0; s < count; s++)
    {
        int acc;
        unsigned int c;

        acc = 0;
        for (c = 0; c < (unsigned int)numChannelsToMix; c++)
        {
            S3mChannel *ch;
            S3mSample  *sm;
            unsigned long idx;

            ch = &channels[c];
            if (!ch->playing) continue;
            sm = ch->samplePtr;
            if (!sm) continue;   /* filet de sécurité : ne devrait pas arriver
                                     tant que playing=1 (voir applyCell) */

            idx = ch->pos >> 16;

            /* Chemin rapide : cas de très loin le plus fréquent
               (pas de bouclage d'échantillon actif), sans appel de
               fonction. Le cas bouclé (rare, un seul échantillon
               "instrument" concerné en général) passe par
               resolveIndex(), plus coûteux mais peu fréquent. */
            if (!sm->loop)
            {
                if (idx >= sm->len) { ch->playing = 0; continue; }
            }
            else
            {
                int stop;
                idx = resolveIndex(sm, idx, &stop);
                if (stop) { ch->playing = 0; continue; }
            }
            ch->pos = (idx << 16) | (ch->pos & 0xFFFFUL);

            /* Table de conversion volume précalculée par tick, adressée
               via le pointeur mis en cache (voir déclaration de
               S3mChannel) : plus d'indexage chanVolTable[c] ici, une
               simple lecture indexée sur ch->volTablePtr suffit. */
            acc += ch->volTablePtr[sm->data[idx]];

            ch->pos += ch->step;
        }

        if (acc > 127)  acc = 127;
        if (acc < -128) acc = -128;
        dst[s] = (unsigned char)(acc + 128);
    }
}

/* ---------------------------------------------------------
   API publique de mixage
   --------------------------------------------------------- */

void s3mMix(unsigned char far *buf, unsigned int n)
{
    unsigned int done;
    unsigned int chunk;

    if (!s3mLoaded || !s3mPlaying)
    {
        _fmemset(buf, 128, n);
        return;
    }

    done = 0;
    while (done < n)
    {
        if (samplesLeftInTick == 0)
        {
            doTick();
            if (!s3mPlaying)
            {
                _fmemset(buf + done, 128, (size_t)(n - done));
                return;
            }
            samplesLeftInTick = samplesPerTick;
        }

        chunk = n - done;
        if (chunk > samplesLeftInTick) chunk = samplesLeftInTick;

        mixChunk(buf + done, chunk);

        done              += chunk;
        samplesLeftInTick -= chunk;
    }
}

/* ---------------------------------------------------------
   État de lecture / bouclage — interrogation depuis l'extérieur
   (audio.c), pour piloter des scènes sur l'état réel du morceau
   plutôt que sur une durée estimée à la main.
   --------------------------------------------------------- */

int s3mIsPlaying(void)
{
    return (s3mLoaded && s3mPlaying) ? 1 : 0;
}

int s3mConsumeLoopFlag(void)
{
    int r = loopFlag;
    loopFlag = 0;
    return r;
}

/* ---------------------------------------------------------
   Fondu (fade-in / fade-out) — voir déclaration des variables
   fade* plus haut dans le fichier.
   --------------------------------------------------------- */

void s3mFadeTo(int targetPercent, unsigned long durationMs)
{
    if (targetPercent < 0)   targetPercent = 0;
    if (targetPercent > 100) targetPercent = 100;

    /* Départ = niveau ACTUEL (pas 127 par hypothèse) : appeler
       s3mFadeTo() en plein milieu d'un fondu précédent repart
       proprement de là où on en est, sans saut audible. */
    fadeStart  = fadeLevel;
    fadeTarget = (targetPercent * 127) / 100;

    fadeSamplePos       = 0;
    fadeDurationSamples = (durationMs == 0) ? 0
                        : ((unsigned long)durationMs * mixRate) / 1000UL;

    if (fadeDurationSamples == 0)
    {
        /* Durée nulle (ou arrondie à 0 échantillon) : fondu instantané,
           pas de rampe à faire avancer dans doTick(). */
        fadeLevel = fadeTarget;
    }
}
