// view.c
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include "game_state.h"

#define SHM_STATE "/game_state"
#define SHM_SYNC "/game_sync"


#define COLOR_RESET "\033[0m" // Reset
#define COLOR_PLAYER_1 "\033[41m" // Rojo
#define COLOR_PLAYER_2 "\033[42m" // Verde
#define COLOR_PLAYER_3 "\033[43m" // Amarillo
#define COLOR_PLAYER_4 "\033[44m" // Azul
#define COLOR_PLAYER_5 "\033[45m" // Magenta
#define COLOR_PLAYER_6 "\033[46m" // Cian
#define COLOR_PLAYER_7 "\033[100m" // Gris oscuro
#define COLOR_PLAYER_8 "\033[47m" // Blanco
#define COLOR_PLAYER_9 "\033[101m" // Rojo claro

const char* get_player_color(int player_id) {
    switch (player_id) {
        case 0: return COLOR_PLAYER_1;
        case 1: return COLOR_PLAYER_2;
        case 2: return COLOR_PLAYER_3;
        case 3: return COLOR_PLAYER_4;
        case 4: return COLOR_PLAYER_5;
        case 5: return COLOR_PLAYER_6;
        case 6: return COLOR_PLAYER_7;
        case 7: return COLOR_PLAYER_8;
        case 8: return COLOR_PLAYER_9;
        default: return COLOR_RESET; // Sin color
    }
}


void mostrar_estado(GameState* estado) {
    
    printf("\n----TABLERO----\n");
    for (int i = 0; i < estado->height; i++) {
        for (int j = 0; j < estado->width; j++) {

            int cell_value = estado->tablero[i * estado->width + j];
            if (cell_value <= 0) {
                printf("%s%2d %s", get_player_color(-cell_value), cell_value, COLOR_RESET);
            }else{
                printf("%2d ", cell_value);
            }

        }
        printf("\n");
    }

    printf("\n----JUGADORES----\n");
    for (int i = 0; i < estado->cantidad_jugadores; i++) {
        Player* jugador = &estado->jugadores[i];
        printf("Jugador %d: %s%s%s, PID: %d, Puntaje: %u, Movimientos válidos: %u, Movimientos inválidos: %u, Posición: (%hu, %hu), Bloqueado: %s\n",
               i, get_player_color(i), jugador->nombre, COLOR_RESET, jugador->pid, jugador->puntaje,
               jugador->valid_moves, jugador->invalid_moves,
               jugador->x, jugador->y, jugador->bloqueado ? "Sí" : "No");
    }
    printf("\nEstado del juego: %s\n", estado->terminado ? "Terminado" : "En curso");
    printf("----------------\n");
    fflush(stdout);
}

int main(int argc, char *argv[]) {
    printf("[view] Iniciando vista...\n");

    // Abrir memoria compartida del estado del juego
    int shm_fd = shm_open(SHM_STATE, O_RDONLY, 0);
    if (shm_fd < 0) {
        perror("[view] shm_open state");
        return 1;
    }

    GameState* state = mmap(NULL, sizeof(GameState), PROT_READ, MAP_SHARED, shm_fd, 0);
    if (state == MAP_FAILED) {
        perror("[view] mmap state");
        return 1;
    }

    // Abrir memoria compartida de sincronización
    int sync_fd = shm_open(SHM_SYNC, O_RDWR, 0666);
    if (sync_fd < 0) {
        perror("[view] shm_open sync");
        return 1;
    }

    SyncState* sync = mmap(NULL, sizeof(SyncState), PROT_READ | PROT_WRITE, MAP_SHARED, sync_fd, 0);
    if (sync == MAP_FAILED) {
        perror("[view] mmap sync");
        return 1;
    }

    printf("[view] Memorias mapeadas correctamente.\n");

    while (!state->terminado) {
        // Mover el cursor al inicio de la pantalla y limpiar desde ahí
        
        // Esperar a que el máster indique que hay algo que imprimir
        sem_wait(&sync->A);
        
        // Leer el estado del juego
        printf("\033[H\033[J"); // \033[H mueve el cursor al inicio, \033[J limpia desde el cursor hasta el final
        mostrar_estado(state);
        
        // Indicar al máster que ya imprimió
        sem_post(&sync->B);
    }

    printf("[view] Juego terminado.\n");
    // Desmapear memoria compartida
    if (munmap(state, sizeof(GameState)) == -1) {
        perror("[view] munmap state");
        return 1;
    }
    if (munmap(sync, sizeof(SyncState)) == -1) {
        perror("[view] munmap sync");
        return 1;
    }
    // Cerrar descriptores de archivo
    if (close(shm_fd) == -1) {
        perror("[view] close shm_fd");
        return 1;
    }
    if (close(sync_fd) == -1) {
        perror("[view] close sync_fd");
        return 1;
    }
    

    return 0;
}

