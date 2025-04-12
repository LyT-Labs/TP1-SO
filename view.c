// view.c
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdlib.h>
#include "game_state.h"

#define BOLD "\033[1m" // Negrita
#define UNDERLINE "\033[4m" // Subrayado
#define RESET "\033[0m" // Reset

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

#define SYMBOL_PLAYER_1 "🐙"
#define SYMBOL_PLAYER_2 "🦎"
#define SYMBOL_PLAYER_3 "🐥"
#define SYMBOL_PLAYER_4 "🐬"
#define SYMBOL_PLAYER_5 "🦄"
#define SYMBOL_PLAYER_6 "🐋"
#define SYMBOL_PLAYER_7 "🐜"
#define SYMBOL_PLAYER_8 "🐏"
#define SYMBOL_PLAYER_9 "🪱"

#define SYMBOL_DIVIDER "─"


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


void print_divider(int width) {
    for (int i = 0; i < width; i++) {
        printf(SYMBOL_DIVIDER);
    }
}

void print_divider_with_title(int width, const char* title) {
    int title_length = strlen(title);
    int padding = (width - title_length) / 2;

    // Print the divider up to the title
    print_divider(padding);

    // Print the title
    printf("%s", title);

    // Print the remaining divider
    print_divider(padding + (width - title_length) % 2);
}


void render_board_section(GameState* state) {
    int width = state->width;
    int height = state->height;

    // Calcular el ancho total del tablero para centrar "TABLERO"
    int tablero_width = width * 4; // Cada celda tiene 3 espacios, más los bordes
    int texto_length = 7; // Longitud de la palabra "TABLERO"
    int padding = (tablero_width - texto_length) / 2;

    // Imprimir la palabra "TABLERO" centrada
    printf("%s", BOLD);
    print_divider_with_title(tablero_width, "TABLERO");
    printf("%s\n", RESET);



    // Imprimir las filas del tablero
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int cell_value = state->board[y * width + x];
            if (cell_value <= 0) {
                printf("%s %s %s", get_player_color(-cell_value), get_player_symbol(-cell_value), COLOR_RESET);
            } else {
                printf(" %2d ", cell_value);
            }
        }
        printf("\n");
    }

    // Imprimir línea divisoria final
    print_divider(tablero_width);
    printf("\n");
}

void print_centered(const char* text, int width) {
    int len = strlen(text);
    if (len >= width) {
        printf("%.*s", width, text); // Truncar si es más largo
        return;
    }
    int padding = (width - len) / 2;
    int extra = (width - len) % 2; // Por si es impar
    printf("%*s%s%*s", padding, "", text, padding + extra, "");
}

void render_players_section(GameState* state) {
    int width = state->width;

    // Calcular el ancho total del tablero para centrar "JUGADORES"
    int tablero_width = width * 4; // Cada celda tiene 3 espacios, más los bordes
    int texto_length = 9; // Longitud de la palabra "JUGADORES"
    int padding = (tablero_width - texto_length) / 2;

    // Imprimir la palabra "JUGADORES" centrada
    printf("\n\n%s", BOLD);
    print_divider_with_title(24+15*6, "JUGADORES");
    printf("%s\n", RESET);

    // Imprimir encabezados centrados
    printf("%s", RESET);
    printf("%s", BOLD);
    print_centered("Jugador",   24);
    print_centered("PID",       15);
    print_centered("Puntaje",   15);
    print_centered("Validos",   15);
    print_centered("Invalidos", 15);
    print_centered("Posicion",  15);
    print_centered("Bloqueado", 15);
    // print_centered("Nombre",    12);
    printf("%s\n", RESET);

    // Imprimir los jugadores
    for (int i = 0; i < state->player_count; i++) {
        Player* jugador = &state->players[i];
        printf("%s", get_player_color(i));
        
        char name[16];
        snprintf(name, sizeof(name), "%s", jugador->name);
        printf("%8s  ", get_player_symbol(i));
        printf("%-16s", name);
        
        char pid[15];
        snprintf(pid, sizeof(pid), "%d", jugador->pid);
        print_centered(pid, 15);

        char score[15];
        snprintf(score, sizeof(score), "%u", jugador->score);
        print_centered(score, 15);

        // Imprimir el número de movimientos válidos e inválidos
        char valid_moves[15];
        snprintf(valid_moves, sizeof(valid_moves), "%u", jugador->valid_moves);
        print_centered(valid_moves, 15);

        char invalid_moves[15];
        snprintf(invalid_moves, sizeof(invalid_moves), "%u", jugador->invalid_moves);
        print_centered(invalid_moves, 15);
        
        char position[15];
        sprintf(position, "(%02hu,%02hu)", jugador->x, jugador->y);
        print_centered(position, 15);
        
        print_centered(jugador->is_blocked ? "SI" : "NO", 15);

        printf("%s\n", COLOR_RESET);
    }

    // Imprimir línea divisoria final
    print_divider(24+15*6);
    printf("\n");
}



void print_state(GameState* state) {
    
    render_board_section(state);
    render_players_section(state);

    fflush(stdout);
}

int main(int argc, char *argv[]) {
    printf("[view] Iniciando vista...\n");
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ancho> <alto>\n", argv[0]);
        return 1;
    }
    unsigned short width = (unsigned short)atoi(argv[1]);
    unsigned short height = (unsigned short)atoi(argv[2]);

    // Abrir memoria compartida del estado del juego
    int shm_fd = shm_open(SHM_STATE, O_RDONLY, 0);
    if (shm_fd < 0) {
        perror("[view] shm_open state");
        return 1;
    }

    GameState* state = mmap(NULL, sizeof(GameState) + sizeof(int) * width * height, PROT_READ, MAP_SHARED, shm_fd, 0);
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

    while (!state->is_finished) {
        // Mover el cursor al inicio de la pantalla y limpiar desde ahí
        
        // Esperar a que el máster indique que hay algo que imprimir
        sem_wait(&sync->changes_available);
        
        // Leer el estado del juego
        printf("\033[H\033[J"); // \033[H mueve el cursor al inicio, \033[J limpia desde el cursor hasta el final
        print_state(state);
        sleep(0); 
        // Indicar al máster que ya imprimió
        sem_post(&sync->print_done);
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

