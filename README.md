# Système de commande de restaurant

## Contexte
Ce projet simule le fonctionnement d’un restaurant dans lequel :
- des **serveurs** prennent des commandes,
- des **cuisiniers** préparent les plats correspondants.

Le système est implémenté en langage **C** et repose sur l’utilisation de **threads**, **mutex** et **sémaphores** afin de gérer correctement la concurrence entre les différents acteurs.

---

## Fonctionnalités implémentées
- Les serveurs ajoutent des commandes dans une **file d’attente partagée**.
- Les cuisiniers récupèrent les commandes depuis cette file pour les traiter.
- La file d’attente est protégée par un **mutex** pour éviter les accès concurrents.
- Des **sémaphores** sont utilisés pour :
  - limiter le nombre de commandes en attente,
  - synchroniser producteurs (serveurs) et consommateurs (cuisiniers).
- Affichage en temps réel de l’état du système (ajout et traitement des commandes).

---

## Objectifs pédagogiques
- Mise en œuvre du **modèle producteur-consommateur**.
- Compréhension et utilisation avancée de :
  - threads POSIX (`pthread`)
  - mutex
  - sémaphores
- Gestion de la synchronisation dans un environnement concurrent.

---

## Structure du projet
Structure du projet

Structure du projet

- main.c
  Programme principal : création des threads, logique du restaurant et affichage.
- queue.c
  Implémentation de la file d’attente des commandes (fonctions de gestion).
- queue.h
  Déclarations des structures et fonctions de la file d’attente.
- .gitignore
  Exclusion des fichiers générés (binaires, fichiers objets).
- README.md
  Documentation du projet.

## 🛠️ Compilation
Sous Linux (Ubuntu) :

```bash
gcc -pthread main.c queue.c -o restaurant

./restaurant
