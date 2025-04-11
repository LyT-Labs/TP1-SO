// view.c
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include "game_state.h"

#define SHM_STATE "/game_state"
#define SHM_SYNC "/game_sync"


void mostrar_estado(GameState* estado) {
    
    printf("\n----TABLERO----\n");
    for (int i = 0; i < estado->height; i++) {
        for (int j = 0; j < estado->width; j++) {

            int cell_value = estado->tablero[i * estado->width + j];
            if (cell_value == 0) {
                printf("\033[47m%2d \033[0m", cell_value); // Fondo blanco
            } else if (cell_value == -1) {
                printf("\033[41m%2d \033[0m", cell_value); // Fondo rojo
            } else if (cell_value == -2) {
                printf("\033[44m%2d \033[0m", cell_value); // Fondo azul
            } else {
                printf("%2d ", cell_value); // Sin fondo
            }
        }
        printf("\n");
    }

    printf("\n----JUGADORES----\n");
    for (int i = 0; i < estado->cantidad_jugadores; i++) {
        Player* jugador = &estado->jugadores[i];
        printf("Jugador %d: %s, PID: %d, Puntaje: %u, Movimientos válidos: %u, Movimientos inválidos: %u, Posición: (%hu, %hu), Bloqueado: %s\n",
               i, jugador->nombre, jugador->pid, jugador->puntaje,
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

