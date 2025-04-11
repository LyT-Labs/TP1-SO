// game_state.h
#pragma once
#include <stdbool.h>
#include <semaphore.h>
#include <sys/types.h>

#define MAX_JUGADORES 9
#define MAX_NOMBRE 16
#define TABLERO_MAX 100

#define SHM_STATE "/game_state"
#define SHM_SYNC "/game_sync"


// Información por jugador
typedef struct {
    char nombre[MAX_NOMBRE];
    unsigned int puntaje;
    unsigned int invalid_moves;
    unsigned int valid_moves;
    unsigned short x, y;
    pid_t pid;
    bool bloqueado;
} Player;

// Estado global
typedef struct {
    unsigned short width;
    unsigned short height;
    unsigned int cantidad_jugadores;
    Player jugadores[MAX_JUGADORES];
    bool terminado;
    int tablero[]; // fila 0, fila 1, ..., fila n-1
} GameState;

// Estructura de sincronización
typedef struct {
    sem_t A; // máster → vista: hay algo que imprimir
    sem_t B; // vista → máster: ya imprimí
    sem_t C; // máster: exclusión mutua para evitar inanición
    sem_t D; // mutex del estado del juego
    sem_t E; // mutex para la variable F
    unsigned int F; // cantidad de jugadores leyendo
} SyncState;


