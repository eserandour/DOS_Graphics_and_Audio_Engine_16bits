/* =========================================================
   TIMER.C — Timer calé sur le retour vertical de l'écran
   =========================================================
   Principe général : voir l'en-tête de timer.h.
   ========================================================= */

#include <dos.h>    /* _disable, _enable, _dos_getvect,
                       _dos_setvect, _chain_intr            */
#include <conio.h>  /* inp, outp                            */
#include "timer.h"

/* ---------------------------------------------------------
   Réglages du calage sur le retour vertical
   (unités PIT : 1 unité = 1 / 1 193 180 s ≈ 0,84 µs ;
   une image à 70,086 Hz ≈ 17 025 unités)
   --------------------------------------------------------- */

/* Avance de l'interruption sur le retour vertical prévu.
   L'ISR attend ce temps-là en boucle à chaque image (~3 % du
   temps CPU) ; il couvre la latence d'interruption, plus
   longue quand le processeur était en mode réel (appel DOS). */
#define VS_MARGIN      512U    /* ~0,43 ms */

/* Retard au-delà de VS_MARGIN encore accepté à l'entrée de
   l'ISR. L'impulsion de synchro verticale (bit 3 du port
   0x3DA) ne dure que ~2 lignes (~64 µs ≈ 76 unités) : arrivé
   plus tard, on l'aurait ratée et l'on attendrait une image
   entière, donc on passe directement en roue libre. */
#define VS_TOLERANCE   160U

/* Retard maximal (depuis l'échéance du canal 0) jusqu'auquel
   l'ISR attend le retour vertical avant d'abandonner. */
#define VS_SPIN_MAX    (VS_MARGIN + 1024U)   /* ~1,3 ms */

/* Plus petit rechargement accepté pour le canal 0 : en dessous,
   l'interruption suivante tomberait presque tout de suite. */
#define VS_MIN_RELOAD  1024U

/* Mesure : nombre d'images mesurées, écart toléré autour de la
   médiane, attente maximale d'un retour vertical (~33 ms, soit
   plus d'une image même à 30 Hz), plage de fréquences admise. */
#define CAL_SAMPLES    9
#define CAL_SPREAD     32U
#define CAL_TIMEOUT    40000U
#define CAL_MIN_PERIOD 12969U  /* 1193180 / 92 Hz */
#define CAL_MAX_PERIOD 19886U  /* 1193180 / 60 Hz */

/* ---------------------------------------------------------
   Variables du module (timer_ticks est exportée via timer.h,
   les autres sont static, donc invisibles hors de ce fichier)
   --------------------------------------------------------- */

/* Compteur global de ticks, incrémenté par l'ISR.
   volatile : le compilateur ne doit pas l'optimiser car
   sa valeur peut changer à tout moment (interruption). */
volatile unsigned long timer_ticks = 0;

/* Sauvegarde du pointeur vers l'ISR timer originale du BIOS.
   On la restaurera à la sortie du programme. */
static void interrupt (far *old_timer_isr)();

/* Accumulateur pour le chaînage de l'ISR BIOS.
   Le BIOS attend une interruption toutes les 65536 unités
   PIT. On simule cela en accumulant, à chaque interruption,
   le nombre d'unités PIT écoulées depuis la précédente, et
   en appelant le BIOS quand on dépasse 65536. */
static unsigned long accum = 0;

/* État du calage sur le retour vertical. */
static int                     vsLocked  = 0;   /* 1 : calé ; 0 : repli PIT libre    */
static unsigned int            vsPeriod  = 0;   /* période d'image mesurée (unités)  */
static volatile unsigned long  vsMisses  = 0;   /* retours verticaux estimés         */
static void                  (*vsHook)(void) = 0;  /* crochet appelé après le vsync */

/* Le code exécuté pendant l'interruption ne doit pas contenir
   de contrôle de pile : l'interruption peut tomber pendant un
   appel DOS ou BIOS, sur une autre pile que celle du programme,
   et le contrôle croirait alors à un débordement. */
#pragma off (check_stack);

/* ---------------------------------------------------------
   Accès au PIT
   ---------------------------------------------------------
   Port 0x43 : registre de contrôle. Un octet de la forme
   cc 00 0000 « verrouille » (latch) le compteur du canal cc :
   les deux lectures suivantes du port de données (0x40 pour
   le canal 0, 0x42 pour le canal 2) rendent l'octet bas puis
   l'octet haut de la valeur figée au moment du verrouillage.
   --------------------------------------------------------- */

static unsigned int pit0Read(void)
{
    unsigned int lo, hi;
    outp(0x43, 0x00);          /* verrouiller le canal 0 */
    lo = inp(0x40);
    hi = inp(0x40);
    return (hi << 8) | lo;
}

/* Unités PIT écoulées depuis l'échéance du canal 0.
   En mode 0, le compteur descend jusqu'à 0 (l'interruption
   part à ce moment-là) puis CONTINUE de descendre en
   repassant par 0xFFFF : 65536 - valeur donne donc le temps
   écoulé depuis l'échéance (modulo 65536, soit 55 ms). */
static unsigned int pit0Late(void)
{
    return (unsigned int)((0x10000UL - pit0Read()) & 0xFFFFUL);
}

/* Programme le canal 0 en mode 0 (« interrupt on terminal
   count ») : une seule interruption, count unités plus tard.
   0x30 = 00 11 000 0 : canal 0, octet bas puis haut, mode 0,
   binaire. L'écriture du mot de contrôle remet la sortie du
   canal à 0 ; elle remonte à l'échéance, et ce front montant
   déclenche l'IRQ 0. */
static void pit0Arm(unsigned int count)
{
    outp(0x43, 0x30);
    outp(0x40, count & 0xFF);
    outp(0x40, count >> 8);
}

/* ---------------------------------------------------------
   Calage dans l'ISR
   ---------------------------------------------------------
   Appelée à chaque interruption quand le timer est calé.
   Attend le retour vertical, réarme le canal 0 pour l'image
   suivante et retourne le nombre de retours verticaux à
   compter (1 en temps normal, davantage si l'interruption a
   été retardée de plus d'une image).
   --------------------------------------------------------- */
static unsigned int vsyncStep(void)
{
    unsigned int  late = pit0Late();
    unsigned int  since, frames;
    unsigned long reload;

    if (late <= VS_MARGIN + VS_TOLERANCE)
    {
        /* À l'heure : attendre le début du retour vertical. */
        while (!(inp(0x3DA) & 0x08))
        {
            late = pit0Late();
            if (late > VS_SPIN_MAX)
                break;              /* pas vu : roue libre */
        }

        if (inp(0x3DA) & 0x08)
        {
            /* Retour vertical en cours : la prochaine
               interruption tombera VS_MARGIN avant le suivant. */
            pit0Arm(vsPeriod - VS_MARGIN);
            return 1;
        }
    }

    /* Roue libre : on suppose que le retour vertical a eu lieu
       à l'instant prévu (VS_MARGIN après l'échéance), et l'on
       réarme par rapport à cet instant. L'image suivante se
       recalera d'elle-même. */
    vsMisses++;
    since  = (late > VS_MARGIN) ? late - VS_MARGIN : 0;
    frames = 1 + since / vsPeriod;          /* images entières manquées */
    since %= vsPeriod;                      /* avance dans l'image en cours */
    reload = (unsigned long)vsPeriod - VS_MARGIN - since;
    if (reload < VS_MIN_RELOAD)
    {
        /* Le prochain retour vertical est trop proche pour
           être attendu : on le compte tout de suite et l'on
           vise le suivant. */
        reload += vsPeriod;
        frames++;
    }
    pit0Arm((unsigned int)reload);
    return frames;
}

/* ---------------------------------------------------------
   ISR principale — une fois par image (ou à 70 Hz en repli)
   --------------------------------------------------------- */

static void interrupt new_timer_isr(void)
{
    unsigned int  frames;
    unsigned long step;

    if (vsLocked)
    {
        frames = vsyncStep();
        timer_ticks += frames;

        /* Juste après le retour vertical : c'est le moment
           d'envoyer la palette en attente (voir palette.h). */
        if (vsHook)
            vsHook();

        step = (unsigned long)vsPeriod * frames;
    }
    else
    {
        timer_ticks++;
        step = DIVISOR;
    }

    /* Pourquoi cet accumulateur ?
       Notre ISR tourne à environ 70 Hz, mais le BIOS s'attend
       à être appelé à 18,2 Hz (= 1 193 180 / 65536). Si on ne
       l'appelle jamais, l'horloge DOS décroche et certains
       drivers plantent.

       Principe : à chaque interruption, on accumule le nombre
       d'unités PIT écoulées (≈ 17 025 par image, ou DIVISOR =
       17 045 en repli). Quand l'accumulateur franchit 65536,
       on a "simulé" une interruption PIT complète à la
       fréquence BIOS, donc on passe la main au BIOS et on
       retranche 65536 (un seul appel par interruption : un
       éventuel surplus est rattrapé aux suivantes). */
    accum += step;

    if (accum >= 65536UL)
    {
        /* L'accumulateur a dépassé 65536 : c'est le moment
           d'appeler l'ISR BIOS pour qu'elle fasse son
           travail (horloge, moteur de disquette, etc.).
           _chain_intr transfère le contrôle à l'ISR BIOS
           qui enverra elle-même l'EOI (End Of Interrupt)
           au PIC, donc on ne le fait pas ici. Le canal 0 a
           déjà été réarmé : le BIOS n'y touche pas. */
        accum -= 65536UL;
        _chain_intr(old_timer_isr);
    }
    else
    {
        /* On ne chaîne pas vers le BIOS ce coup-ci.
           On doit quand même signaler la fin d'interruption
           au PIC (Programmable Interrupt Controller) en
           envoyant un octet EOI (0x20) au port 0x20,
           sinon le PIC bloquera toutes les interruptions
           suivantes. */
        outp(0x20, 0x20);
    }
}

#pragma on (check_stack);

/* ---------------------------------------------------------
   Mesure de la fréquence d'image (hors interruption)
   ---------------------------------------------------------
   Le canal 2 du PIT sert de chronomètre : il est relié au
   haut-parleur, mais sa porte (bit 0 du port 0x61) peut être
   ouverte sans que le haut-parleur sonne (bit 1 à 0). En
   mode 2 avec un diviseur de 65536, il décompte en boucle
   toutes les 55 ms : la différence de deux lectures donne le
   temps écoulé (modulo 65536) sans toucher au canal 0.
   --------------------------------------------------------- */

static unsigned char savedPort61;

static unsigned int pit2Read(void)
{
    unsigned int lo, hi;
    outp(0x43, 0x80);          /* verrouiller le canal 2 */
    lo = inp(0x42);
    hi = inp(0x42);
    return (hi << 8) | lo;
}

static void pit2Start(void)
{
    savedPort61 = (unsigned char)inp(0x61);
    outp(0x61, (savedPort61 & 0xFC) | 0x01);  /* porte ouverte, haut-parleur coupé */
    outp(0x43, 0xB4);          /* 10 11 010 0 : canal 2, bas/haut, mode 2 */
    outp(0x42, 0);
    outp(0x42, 0);             /* diviseur 0 = 65536 */
}

static void pit2Stop(void)
{
    outp(0x61, savedPort61);
}

/* Attend le DÉBUT du prochain retour vertical (fin du retour
   en cours d'abord, comme waitVRetrace()) et rend dans *stamp
   la valeur du canal 2 à cet instant. Retourne 0 si le retour
   vertical n'arrive pas dans les temps (pas de VGA...). */
static int waitRetraceStamp(unsigned int *stamp)
{
    unsigned int t0;

    t0 = pit2Read();
    while (inp(0x3DA) & 0x08)
        if (((t0 - pit2Read()) & 0xFFFFU) > CAL_TIMEOUT)
            return 0;

    t0 = pit2Read();
    while (!(inp(0x3DA) & 0x08))
        if (((t0 - pit2Read()) & 0xFFFFU) > CAL_TIMEOUT)
            return 0;

    *stamp = pit2Read();
    return 1;
}

/* Mesure la période d'image en unités PIT (médiane de
   CAL_SAMPLES images consécutives). Retourne 0 si la mesure
   échoue ou n'est pas cohérente. Le canal 2 doit tourner
   (pit2Start). Interruptions laissées actives : une mesure
   perturbée par une interruption est écartée par la médiane. */
static unsigned int measureFramePeriod(void)
{
    unsigned int s[CAL_SAMPLES];
    unsigned int prev, cur, tmp, med, close;
    int i, j;

    if (!waitRetraceStamp(&prev))
        return 0;

    for (i = 0; i < CAL_SAMPLES; i++)
    {
        if (!waitRetraceStamp(&cur))
            return 0;
        s[i] = (prev - cur) & 0xFFFFU;   /* le canal 2 décompte */
        prev = cur;
    }

    /* Tri par insertion (9 valeurs) puis médiane. */
    for (i = 1; i < CAL_SAMPLES; i++)
    {
        tmp = s[i];
        for (j = i - 1; j >= 0 && s[j] > tmp; j--)
            s[j + 1] = s[j];
        s[j + 1] = tmp;
    }
    med = s[CAL_SAMPLES / 2];

    if (med < CAL_MIN_PERIOD || med > CAL_MAX_PERIOD)
        return 0;

    /* Au moins les deux tiers des mesures près de la médiane. */
    close = 0;
    for (i = 0; i < CAL_SAMPLES; i++)
        if (s[i] + CAL_SPREAD >= med && s[i] <= med + CAL_SPREAD)
            close++;
    if (close * 3 < CAL_SAMPLES * 2)
        return 0;

    return med;
}

/* ---------------------------------------------------------
   Installation du timer
   --------------------------------------------------------- */

void installTimer(void)
{
    unsigned int period, stamp;

    /* 1. Mesure de l'écran (environ 10 images, ~0,15 s),
          avant d'installer quoi que ce soit. */
    pit2Start();
    period = measureFramePeriod();

    /* Désactiver les interruptions pendant la reconfiguration
       pour éviter qu'une interruption timer se déclenche
       pendant qu'on est en train de modifier les vecteurs. */
    _disable();

    /* Sauvegarder l'ISR originale du BIOS (vecteur 08h)
       pour pouvoir la restaurer à la sortie. */
    old_timer_isr = _dos_getvect(0x08);

    /* Installer notre propre ISR à la place. */
    _dos_setvect(0x08, new_timer_isr);

    vsLocked = 0;
    accum    = 0;

    /* 2. Calage : attendre un retour vertical (interruptions
          coupées, au plus une image) et armer aussitôt le
          canal 0 pour qu'il sonne VS_MARGIN avant le suivant. */
    if (period && waitRetraceStamp(&stamp))
    {
        vsPeriod = period;
        pit0Arm(vsPeriod - VS_MARGIN);
        vsLocked = 1;
    }
    else
    {
        /* Repli : PIT libre à TARGET_HZ, comme à l'origine.
           Port 0x43 = registre de contrôle du PIT.
           0x36 = 0011 0110 en binaire :
             bits 7-6 : 00 = canal 0
             bits 5-4 : 11 = accès octet bas puis octet haut
             bits 3-1 : 011 = mode 3 (onde carrée)
             bit  0   : 0 = binaire (pas BCD) */
        outp(0x43, 0x36);

        /* Charger le diviseur dans le PIT (canal 0, port 0x40)
           en deux fois : octet bas d'abord, octet haut ensuite. */
        outp(0x40, DIVISOR & 0xFF);   /* octet bas  */
        outp(0x40, DIVISOR >> 8);     /* octet haut */
    }

    pit2Stop();

    /* Réactiver les interruptions. */
    _enable();
}

/* ---------------------------------------------------------
   Restauration du timer
   --------------------------------------------------------- */

void restoreTimer(void)
{
    /* Désactiver les interruptions pendant la restauration. */
    _disable();

    vsLocked = 0;

    /* Remettre l'ISR BIOS originale sur le vecteur 08h. */
    _dos_setvect(0x08, old_timer_isr);

    /* Remettre le PIT à sa fréquence par défaut (~18,2 Hz),
       en mode 3 (onde carrée) comme le BIOS.
       Charger 0 dans le diviseur = 65536, ce qui donne
       1193180 / 65536 ≈ 18,2 Hz. */
    outp(0x43, 0x36);
    outp(0x40, 0);   /* octet bas  = 0 */
    outp(0x40, 0);   /* octet haut = 0 → diviseur = 65536 */

    /* Réactiver les interruptions. */
    _enable();
}

void timerResync(void)
{
    restoreTimer();
    installTimer();
}

/* ---------------------------------------------------------
   État du calage
   --------------------------------------------------------- */

int timerIsVSyncLocked(void)
{
    return vsLocked;
}

unsigned long timerGetRefreshMilliHz(void)
{
    if (vsLocked)
        return (PIT_FREQ * 1000UL) / vsPeriod;
    return (PIT_FREQ * 1000UL) / DIVISOR;
}

unsigned long timerGetVSyncMisses(void)
{
    return vsMisses;
}

void timerSetVSyncHook(void (*hook)(void))
{
    _disable();
    vsHook = hook;
    _enable();
}

void (*timerGetVSyncHook(void))(void)
{
    return vsHook;
}

/* ---------------------------------------------------------
   Lecture du timer
   --------------------------------------------------------- */

/* Retourne le nombre de ticks depuis installTimer(). */
unsigned long readTimer(void)
{
    return timer_ticks;
}

/* ---------------------------------------------------------
   Calcul du temps écoulé
   --------------------------------------------------------- */

/* Retourne stop - start en ticks, en gérant le débordement.
   Quand timer_ticks dépasse 0xFFFFFFFF il repasse à 0.
   Si stop < start c'est qu'il y a eu débordement. */
unsigned long elapsedTime(unsigned long start, unsigned long stop)
{
    if (stop >= start)
        return stop - start;

    /* Cas de débordement : on calcule la distance en
       faisant le tour complet de l'espace 32 bits. */
    return (0xFFFFFFFFUL - start) + stop + 1;
}

/* Convertit un écart de ticks en millisecondes.
   Formule : ticks * 1000 / TARGET_HZ. */
unsigned long elapsedTimeMs(unsigned long start, unsigned long stop)
{
    return (elapsedTime(start, stop) * 1000UL) / TARGET_HZ;
}

/* ---------------------------------------------------------
   Pause précise
   --------------------------------------------------------- */

/* Attente active (busy-wait) pendant ms millisecondes.
   Convertit ms en ticks (ms * TARGET_HZ / 1000), puis
   boucle jusqu'à ce que timer_ticks ait avancé d'autant.

   Pourquoi le NOP dans la boucle ?
   Sans instruction, certains compilateurs suppriment la
   boucle entière (ils supposent qu'une boucle sans effet
   de bord est inutile). Le NOP force la génération d'une
   instruction réelle et préserve le comportement d'attente.
   timer_ticks est volatile, ce qui protège déjà en théorie,
   mais le NOP est une garantie explicite supplémentaire. */
void pause(unsigned long ms)
{
    unsigned long start          = timer_ticks;
    unsigned long ticks_to_wait  = (ms * TARGET_HZ) / 1000UL;

    while ((unsigned long)(timer_ticks - start) < ticks_to_wait)
        timerWaitChange(timer_ticks);   /* dort jusqu'au tick suivant */
}

/* ---------------------------------------------------------
   Attente économe (voir timer.h)
   --------------------------------------------------------- */

static int idleOn = 1;    /* timerSetIdle() */

void timerSetIdle(int on)
{
    idleOn = on;
}

void timerWaitChange(unsigned long t)
{
    if (!idleOn)
    {
        while (timer_ticks == t)
            ;                       /* timer_ticks est volatile */
        return;
    }

    for (;;)
    {
        _disable();                 /* pas d'IRQ entre le test et HLT */
        if (timer_ticks != t)
        {
            _enable();
            return;
        }
        _asm {
            sti                     /* effectif après l'instruction suivante */
            hlt                     /* réveil à la prochaine interruption    */
        }
    }
}
