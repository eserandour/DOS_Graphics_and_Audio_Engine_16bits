#ifndef SBLASTER_H
#define SBLASTER_H

/* =========================================================
   SBLASTER.H — Pilote bas niveau Sound Blaster (DSP + DMA)
   =========================================================
   Environnement : Open Watcom 1.9, DOS

   Ce module ne connait ni le S3M ni le WAV : il sait
   uniquement dialoguer avec le DSP Sound Blaster (reset,
   version, constante de temps, volume mixer) et avec le
   contrôleur DMA 8237 (canal 8 bits, mode auto-init en
   boucle, programmé une seule fois, sans réarmement à
   chaque IRQ — voir sbStartOutputLoop() plus bas et audio.c).

   DÉTECTION
   ---------
   La détection se fait via la variable d'environnement
   BLASTER, positionnée par le pilote SB (SET BLASTER=A220
   I5 D1 H5 P330 T6). Si elle est absente ou mal formée,
   sbDetect() échoue et TOUT le reste du moteur audio doit
   rester silencieux sans jamais planter (voir audio.c).

   BUFFER DMA
   ----------
   Un buffer utilisé en DMA ne doit jamais franchir une
   frontière physique de 64 Ko (limite du registre de page
   du 8237). sbAllocDmaBuffer() alloue le double de la
   taille demandée et choisit la moitié qui ne franchit pas
   cette frontière — technique classique, garantie correcte
   pour toute taille <= 32767 octets (le double doit tenir dans
   un bloc _fmalloc de 65535 octets au plus).

   FRÉQUENCE
   ---------
   DSP >= 4.00 (Sound Blaster 16) : fréquence exacte en Hz
   (sbSetOutputRate, commande 0x41) et sortie 8 bits auto-init
   par la commande SB16 0xC6. Cartes plus anciennes (DSP 2.xx,
   3.xx) : constante de temps (commande 0x40), qui ne donne que
   1 000 000 / n Hz (22 222 Hz au plus près de 22 050) — audio.c
   mixe alors à la fréquence réellement produite (sbRateFromTc),
   pour que la musique garde sa hauteur et son tempo. La sortie
   auto-init (0x1C) demande un DSP >= 2.00 : un DSP 1.xx est
   refusé par audioInit().
   ========================================================= */

/* ---------------------------------------------------------
   Codes de retour
   --------------------------------------------------------- */
#define SB_OK           0
#define SB_ERR_NOENV    1   /* variable BLASTER absente/illisible */
#define SB_ERR_NORESET  2   /* le DSP ne répond pas au reset      */

/* ---------------------------------------------------------
   Informations carte détectée (lecture seule pour audio.c)
   --------------------------------------------------------- */
extern unsigned int  sbBase;         /* port I/O de base, ex. 0x220 */
extern int           sbIrq;          /* IRQ matériel, ex. 5 ou 7    */
extern int           sbDma8;         /* canal DMA 8 bits, 0-3       */
extern unsigned char sbVersionMajor;
extern unsigned char sbVersionMinor;

/* ---------------------------------------------------------
   Détection et initialisation du DSP
   --------------------------------------------------------- */

/* Lit et parse la variable d'environnement BLASTER.
   Retourne SB_OK ou SB_ERR_NOENV. */
int sbDetect(void);

/* Reset matériel du DSP (impulsion sur le port +0x6) et
   vérification de la réponse 0xAA. Lit aussi la version.
   Retourne SB_OK ou SB_ERR_NORESET. */
int sbReset(void);

/* Programme la constante de temps (fréquence d'échantillonnage).
   Compatible avec toutes les versions de DSP (commande 0x40),
   contrairement à la commande 0x41 réservée aux DSP >= 4.xx. */
void sbSetTimeConstant(unsigned char tc);

/* Fréquence réellement produite par la constante de temps tc :
   1 000 000 / (256 - tc) Hz. */
unsigned long sbRateFromTc(unsigned char tc);

/* 1 si le DSP est un 4.xx ou plus (Sound Blaster 16) : fréquence
   exacte (sbSetOutputRate) et commande 0xC6 disponibles. */
int sbIsDsp4(void);

/* Fréquence de sortie exacte en Hz (commande 0x41, DSP >= 4.00
   uniquement : voir sbIsDsp4). */
void sbSetOutputRate(unsigned int hz);

/* Positionne le volume du mixer matériel (s'il existe, SB Pro/16)
   au maximum. Sans effet — et sans erreur — sur un DSP sans
   puce mixer (SB 1.0/2.0) : dans ce cas il n'y a de toute façon
   aucune atténuation possible, le volume est donc déjà "max". */
void sbSetMixerVolumeMax(void);

/* ---------------------------------------------------------
   Buffer DMA
   --------------------------------------------------------- */

/* Alloue un buffer de 'size' octets (size <= 32767) garanti de
   ne pas franchir une frontière physique de 64 Ko.
   *rawBlock reçoit le pointeur far à passer à _ffree().
   *physAddr reçoit l'adresse physique 20 bits (segment*16+offset)
   à programmer dans le contrôleur DMA.
   Retourne le pointeur far utilisable pour écrire les données,
   ou NULL si l'allocation échoue. */
unsigned char far *sbAllocDmaBuffer(unsigned int size,
                                     unsigned long far *physAddr,
                                     unsigned char far * far *rawBlock);

/* ---------------------------------------------------------
   IRQ
   --------------------------------------------------------- */

/* Installe 'handler' sur le vecteur IRQ détecté par sbDetect(),
   sauvegarde l'ancien vecteur et démasque l'IRQ au PIC.
   À appeler après sbDetect() / sbReset(). */
void sbInstallIRQ(void interrupt (far *handler)());

/* Restaure le vecteur IRQ original et remasque l'IRQ au PIC
   dans son état d'avant sbInstallIRQ(). À appeler impérativement
   avant de quitter le programme. */
void sbRestoreIRQ(void);

/* À appeler en tout début de l'ISR audio : acquitte l'IRQ côté
   DSP (lecture du port de statut 8 bits) puis envoie l'EOI au(x)
   PIC(s) concerné(s) (gère le cas IRQ >= 8, second PIC). */
void sbAckIRQ(void);

/* ---------------------------------------------------------
   Lecture DMA (sortie 8 bits)
   --------------------------------------------------------- */

/* Programme le contrôleur DMA UNE SEULE FOIS en mode auto-init
   sur un buffer circulaire de 2*halfLen octets (physAddr =
   adresse physique du DÉBUT de ce buffer, les deux moitiés
   devant être contiguës — voir sbAllocDmaBuffer), puis démarre
   le DSP en sortie 8 bits auto-init (commande 0x1C, ou 0xC6 sur
   un DSP >= 4.00 : voir FRÉQUENCE plus haut).

   Le DSP boucle ensuite indéfiniment sur ce buffer et lève une
   IRQ tous les halfLen octets, SANS AUCUN réarmement CPU entre
   les blocs : c'est la méthode à utiliser pour une lecture
   continue sans clic (voir audio.c). À appeler une seule fois
   à l'initialisation, jamais depuis l'ISR. */
void sbStartOutputLoop(unsigned long physAddr, unsigned int halfLen);

/* Programme le contrôleur DMA sur physAddr/len puis déclenche
   la commande DSP 0x14 (sortie 8 bits simple-cycle, un seul
   bloc, pas de bouclage matériel) pour 'len' octets. Conservée
   pour compatibilité/tests ponctuels — pour la lecture continue
   du moteur audio, préférer sbStartOutputLoop() (voir plus haut :
   réarmer ce mode à chaque IRQ produit un micro-trou audible
   entre chaque bloc). */
void sbStartOutput(unsigned long physAddr, unsigned int len);

/* Arrête proprement la sortie DMA en cours (pause DSP) — utilisé
   à la fermeture pour éviter tout bruit résiduel ou DMA qui
   continue de tourner après la sortie du programme. */
void sbStopOutput(void);

#endif /* SBLASTER_H */
