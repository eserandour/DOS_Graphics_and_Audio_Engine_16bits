#ifndef TIMER_H
#define TIMER_H

/* =========================================================
   TIMER.H — Timer calé sur le retour vertical de l'écran
   =========================================================
   Le PC dispose d'un circuit appelé PIT (Programmable
   Interval Timer, chip Intel 8253/8254) qui génère des
   interruptions matérielles à intervalles réguliers.

   Par défaut le BIOS programme le PIT à environ 18,2 Hz
   (une interruption toutes les 55 ms). C'est trop lent
   pour animer des scènes fluides.

   Ce module installe sa propre ISR (Interrupt Service Routine)
   sur le vecteur 08h et fait battre le timer AU RYTHME DE
   L'ÉCRAN : un tick = un retour vertical (vsync) de la VGA.

   POURQUOI ?
   L'écran en mode 13h ne tourne pas à 70 Hz pile mais à
   environ 70,086 Hz (25,175 MHz / 800 / 449). Un PIT réglé à
   70,002 Hz décroche d'une image toutes les 12 secondes
   environ : une image sautée ou doublée, bien visible sur un
   défilement. Et une image copiée en VRAM à un instant
   quelconque est coupée en deux (tearing). Avec un tick par
   retour vertical, flip() peut copier l'image juste après le
   retour vertical, et la palette est envoyée au DAC au même
   instant (voir palette.h).

   COMMENT ? (« PLL » logicielle, sans interruption VGA)
   La VGA ne sait pas déclencher d'interruption au retour
   vertical de façon fiable. On s'en approche avec le PIT :
   1. installTimer() mesure la période d'image avec le canal 2
      du PIT (celui du haut-parleur, laissé muet), sur
      plusieurs images, et garde la médiane.
   2. Le canal 0 est programmé en mode 0 (une seule
      interruption par chargement) pour sonner VS_MARGIN
      unités PIT (~0,4 ms) AVANT le retour vertical prévu.
   3. L'ISR attend alors le début du retour vertical (bit 3 du
      port 0x3DA), recharge aussitôt le canal 0 pour l'image
      suivante, incrémente timer_ticks et appelle le crochet
      vsync (envoi de la palette en attente au DAC).
   Le calage est refait à CHAQUE image : aucune dérive
   possible. Si l'ISR arrive trop tard (interruptions masquées
   ailleurs, accès disque...) ou ne voit pas le retour
   vertical, elle continue « en roue libre » sur la période
   mesurée et se recale à l'image suivante
   (timerGetVSyncMisses() compte ces ratés).

   REPLI : si la mesure échoue (pas de VGA, fréquence hors de
   60-92 Hz, mesures incohérentes), le timer revient au
   fonctionnement d'origine : PIT libre à TARGET_HZ (70 Hz),
   sans lien avec l'écran (timerIsVSyncLocked() renvoie 0).

   Dans les deux cas, on ne casse pas les fonctions DOS qui
   dépendent du timer BIOS (horloge, délais...) : l'ISR
   originale est chaînée à la fréquence correcte grâce à un
   accumulateur (voir timer.c).

   ORDRE D'APPEL : installTimer() mesure l'écran, il faut donc
   l'appeler APRÈS setVideoMode(0x13). Après un changement de
   mode vidéo (autre fréquence), appeler timerResync().
   ========================================================= */

/* ---------------------------------------------------------
   Constantes de fréquence
   --------------------------------------------------------- */

/* Fréquence de l'oscillateur interne du PIT en Hz.
   C'est une constante matérielle fixe sur tous les PC. */
#define PIT_FREQ  1193180UL

/* Fréquence nominale du timer en Hz : celle du mode 13h
   arrondie (70,086 Hz réels quand le timer est calé sur
   l'écran), et la fréquence exacte du PIT libre en repli.
   Sert aux conversions ms <-> ticks (MS_TO_TICKS des scènes,
   elapsedTimeMs, pause) : l'écart de 0,12 % avec l'écran
   réel est négligeable (6 ms sur 5 s). */
#define TARGET_HZ 70

/* Valeur à charger dans le PIT pour obtenir TARGET_HZ
   (mode de repli uniquement).
   Le PIT divise sa fréquence d'entrée par ce diviseur.
   Ex : 1193180 / 70 = 17045,4 -> 17045 (division entière,
   tronquée), soit 70,002 Hz réels. */
#define DIVISOR   (PIT_FREQ / TARGET_HZ)

/* ---------------------------------------------------------
   Variable globale
   --------------------------------------------------------- */

/* Compteur de ticks : nombre de retours verticaux depuis
   installTimer() (ou d'interruptions à 70 Hz en repli).
   Incrémenté par l'ISR, parfois de plus de 1 d'un coup si
   des images ont été manquées (le compte reste juste).
   Déclaré volatile car modifié par une ISR (hors du flux
   normal du programme) : empêche le compilateur de le
   mettre en cache dans un registre. */
extern volatile unsigned long timer_ticks;

/* ---------------------------------------------------------
   Fonctions
   --------------------------------------------------------- */

/* Mesure la fréquence de l'écran (environ 0,15 s), installe
   notre ISR sur le vecteur 08h et cale le PIT sur le retour
   vertical (ou, en repli, le règle à TARGET_HZ). À appeler
   une seule fois au démarrage, APRÈS setVideoMode(0x13) et
   avant toute utilisation du timer. */
void installTimer(void);

/* Restaure l'ISR BIOS originale et remet le PIT à 18,2 Hz
   (mode 3 d'origine, quel que soit le mode utilisé).
   À appeler impérativement avant de quitter le programme,
   sinon DOS sera déréglé après la sortie. */
void restoreTimer(void);

/* Refait la mesure et le calage (restoreTimer() puis
   installTimer()). À appeler après un changement de mode
   vidéo qui change la fréquence d'image (ex. 60 Hz). Le timer
   est figé pendant la mesure (environ 0,15 s). */
void timerResync(void);

/* 1 si le timer est calé sur le retour vertical, 0 en repli
   (PIT libre à TARGET_HZ). */
int timerIsVSyncLocked(void);

/* Fréquence réelle d'un tick en millièmes de Hz
   (ex. 70086 pour 70,086 Hz) : mesurée sur l'écran si le
   timer est calé, sinon celle du PIT libre. */
unsigned long timerGetRefreshMilliHz(void);

/* Nombre de retours verticaux que l'ISR n'a pas pu attendre
   (arrivée trop tardive ou retour vertical non vu) et qu'elle
   a remplacés par une estimation. Quelques-uns au démarrage
   ou pendant un accès disque sont normaux ; un compteur qui
   monte sans arrêt signale un mode vidéo qui a changé de
   fréquence (voir timerResync). */
unsigned long timerGetVSyncMisses(void);

/* Crochet appelé par l'ISR juste après chaque retour vertical
   (timer calé uniquement). Il s'exécute DANS l'interruption :
   court, sans float, sans appel à la bibliothèque C ni au DOS.
   main.c y branche paletteVSyncUpload() (voir palette.h).
   L'ISR est en mode réel 16 bits : le crochet aussi, sans
   contrôle de pile (#pragma off (check_stack)).
   NULL pour le retirer. */
void timerSetVSyncHook(void (*hook)(void));

/* Crochet vsync actuellement branché (NULL si aucun). */
void (*timerGetVSyncHook(void))(void);

/* Retourne la valeur actuelle de timer_ticks.
   Préférer cette fonction à l'accès direct à timer_ticks
   pour éventuellement ajouter une section critique plus tard. */
unsigned long readTimer(void);

/* Calcule le nombre de ticks écoulés entre start et stop.
   Gère correctement le débordement du compteur 32 bits
   (qui se produit après 2^32 / 70 ≈ 61,4 millions de
   secondes, soit environ 710 jours ≈ 1 an et 11 mois
   d'exécution continue à 70 Hz). */
unsigned long elapsedTime(unsigned long start, unsigned long stop);

/* Convertit un intervalle de ticks en millisecondes.
   Formule : ticks * 1000 / TARGET_HZ. */
unsigned long elapsedTimeMs(unsigned long start, unsigned long stop);

/* Attend le nombre de millisecondes demandé en bouclant
   sur timer_ticks. Plus précis que la fonction delay()
   standard qui utilise le timer BIOS à 18,2 Hz. */
void pause(unsigned long ms);

/* ---------------------------------------------------------
   Attente économe (HLT)
   ---------------------------------------------------------
   Attend que timer_ticks soit différent de t, en mettant le
   processeur en sommeil (HLT) jusqu'à la prochaine
   interruption (timer, Sound Blaster, clavier) au lieu de
   boucler à vide.

   Sur une vraie machine, le résultat est le même qu'une
   boucle d'attente. Dans un émulateur (DOSBox, DOSBox-X,
   js-dos dans un navigateur), c'est très différent : une
   boucle d'attente consomme tous les cycles émulés, donc
   tout le processeur de la machine hôte, alors qu'un HLT
   rend la main à l'émulateur.

   En mode réel, HLT est toujours permis (sous EMM386 ou dans
   une fenêtre DOS, le moniteur V86 l'intercepte et le traite).

   Pas de tick raté : l'interruption est masquée pendant le
   test, puis STI/HLT s'enchaînent (STI ne prend effet
   qu'après l'instruction suivante), si bien qu'un tick
   arrivé entre le test et le HLT réveille aussitôt. */
void timerWaitChange(unsigned long t);

/* 1 (défaut) : timerWaitChange() dort (HLT) ;
   0 : boucle d'attente classique. */
void timerSetIdle(int on);

#endif /* TIMER_H */
