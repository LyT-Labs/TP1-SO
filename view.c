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
#define COLOR_PLAYER_1 "\033[41m" // Fondo rojo
#define COLOR_PLAYER_2 "\033[42m" // Fondo verde
#define COLOR_PLAYER_3 "\033[43m" // Fondo amarillo
#define COLOR_PLAYER_4 "\033[44m" // Fondo azul
#define COLOR_PLAYER_5 "\033[45m" // Fondo magenta
#define COLOR_PLAYER_6 "\033[46m" // Fondo cian
#define COLOR_PLAYER_7 "\033[100m" // Fondo gris oscuro
#define COLOR_PLAYER_8 "\033[47m" // Fondo blanco
#define COLOR_PLAYER_9 "\033[101m" // Fondo rojo claro

#define SYMBOL_PLAYER_1 "#"
#define SYMBOL_PLAYER_2 "@"
#define SYMBOL_PLAYER_3 "$"
#define SYMBOL_PLAYER_4 "%"
#define SYMBOL_PLAYER_5 "&"
#define SYMBOL_PLAYER_6 "*"
#define SYMBOL_PLAYER_7 "!"
#define SYMBOL_PLAYER_8 "^"
#define SYMBOL_PLAYER_9 "~"


const char* get_player_symbol(int player_id) {
    switch (player_id) {
        case 0: return SYMBOL_PLAYER_1;
        case 1: return SYMBOL_PLAYER_2;
        case 2: return SYMBOL_PLAYER_3;
        case 3: return SYMBOL_PLAYER_4;
        case 4: return SYMBOL_PLAYER_5;
        case 5: return SYMBOL_PLAYER_6;
        case 6: return SYMBOL_PLAYER_7;
        case 7: return SYMBOL_PLAYER_8;
        case 8: return SYMBOL_PLAYER_9;
        default: return " "; // Sin símbolo
    }
}

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
void renderizar_tablero(GameState* estado) {
    int width = estado->width;
    int height = estado->height;

    // Calcular el ancho total del tablero para centrar "TABLERO"
    int tablero_width = width * 4 + 1; // Cada celda tiene 3 espacios más un separador
    int texto_length = 7; // Longitud de la palabra "TABLERO"
    int padding = (tablero_width - texto_length) / 2;

    // Imprimir la línea superior del marco de "TABLERO"
    printf("┌");
    for (int i = 0; i < tablero_width - 2; i++) {
        printf("─");
    }
    printf("┐\n");

    // Imprimir la fila con "TABLERO" centrado
    printf("│");
    for (int i = 0; i < padding; i++) {
        printf(" ");
    }
    printf("TABLERO");
    for (int i = 0; i < padding; i++) {
        printf(" ");
    }
    // Ajustar si el ancho no es divisible exactamente
    if (tablero_width % 2 != texto_length % 2) {
        printf(" ");
    }
    printf("│\n");

    // Imprimir la línea divisoria entre "TABLERO" y el tablero
    printf("├");
    for (int i = 0; i < width; i++) {
        printf("───┬");
    }
    printf("\b┤\n"); // Reemplazar el último "┬" con "┤"

    // Imprimir las filas del tablero
    for (int y = 0; y < height; y++) {
        printf("│"); // Borde izquierdo
        for (int x = 0; x < width; x++) {
            int cell_value = estado->tablero[y * width + x];
            if (cell_value > 0) {
                printf(" %s%s%s │", get_player_color(cell_value - 1), get_player_symbol(cell_value - 1), COLOR_RESET);
            } else {
                printf("   │");
            }
        }
        printf("\n");

        // Imprimir la línea divisoria o inferior
        if (y < height - 1) {
            printf("├");
            for (int i = 0; i < width; i++) {
                printf("───┼");
            }
            printf("\b┤\n"); // Reemplazar el último "┼" con "┤"
        }
    }

    // Imprimir la línea inferior del tablero
    printf("└");
    for (int i = 0; i < width; i++) {
        printf("───┴");
    }
    printf("\b┘\n"); // Reemplazar el último "┴" con "┘"
}

void mostrar_estado(GameState* estado) {
    
    renderizar_tablero(estado);

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

