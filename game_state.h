// game_state.h
#ifndef GAME_STATE_H
#define GAME_STATE_H

#include <stdbool.h>
#include <semaphore.h>
#include <sys/types.h>

#define MAX_PLAYERS 9
#define MAX_NAME 16
#define BOARD_MAX 100

#define SHM_STATE "/game_state"
#define SHM_SYNC "/game_sync"


// Información por jugador
typedef struct {
    char name[MAX_NAME];
    unsigned int score;
    unsigned int invalid_moves;
    unsigned int valid_moves;
    unsigned short x, y;
    pid_t pid;
    bool is_blocked;
} Player;

// Estado global
typedef struct {
    unsigned short width;
    unsigned short height;
    unsigned int player_count;
    Player players[MAX_PLAYERS];
    bool is_finished;
    int board[]; // row 0, row 1, ..., row n-1
} GameState;

// Estructura de sincronización
typedef struct {
    sem_t changes_available;       // máster → vista: hay algo que imprimir
    sem_t print_done;        // vista → máster: ya imprimí
    sem_t starvation_mutex;      // máster: exclusión mutua para evitar inanición
    sem_t game_state_mutex;  // mutex del estado del juego
    sem_t reader_count_mutex;      // mutex para la variable reader_count
    unsigned int reader_count; // cantidad de jugadores leyendo
} SyncState;


#endif // GAME_STATE_H