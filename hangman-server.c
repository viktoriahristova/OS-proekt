#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "game.h"

#define PLAYERS 2
#define MAX_WORD 256
#define MAX_LINE 512

typedef struct {
    int fd;
    secret_word_t word_to_guess;
} player_t;

typedef struct {
    player_t players[PLAYERS];
    int solved[PLAYERS];
    pthread_mutex_t mutex;
    pthread_cond_t finished_cond;
} room_t;

static room_t room;

static int read_line_from_socket(int fd, char *buffer, size_t capacity)
{
    size_t pos = 0;

    while (1) {
        char ch = '\0';
        ssize_t bytes = recv(fd, &ch, 1, 0);
        if (bytes <= 0)
            return -1;

        if (ch == '\n')
            break;

        if (ch != '\r' && pos + 1 < capacity)
            buffer[pos++] = ch;
    }

    buffer[pos] = '\0';
    return (int)pos;
}

static int write_line_to_socket(int fd, const char *format, ...)
{
    char buffer[MAX_LINE];
    va_list args;

    va_start(args, format);
    int length = vsnprintf(buffer, sizeof(buffer) - 2, format, args);
    va_end(args);

    if (length < 0 || length >= (int)sizeof(buffer) - 1)
        return -1;

    buffer[length++] = '\n';
    buffer[length] = '\0';

    ssize_t sent = send(fd, buffer, (size_t)length, MSG_NOSIGNAL);
    return sent == length ? 0 : -1;
}

static void make_visible_word(const secret_word_t *word, char *output, size_t capacity)
{
    size_t pos = 0;

    for (size_t i = 0; i < word->word_length && pos + 1 < capacity; ++i) {
        char letter = '_';
        if (secret_word_letter_at(word, i, &letter) == SECRET_WORD_LETTER_REVEALED)
            output[pos++] = letter;
        else
            output[pos++] = '_';
    }

    output[pos] = '\0';
}

static void make_wrong_letters(const secret_word_t *word, char *output, size_t capacity)
{
    size_t pos = 0;

    for (char letter = 'a'; letter <= 'z'; ++letter) {
        if (!letter_set_contains(word->incorrect_guesses, letter))
            continue;

        if (pos != 0 && pos + 2 < capacity) {
            output[pos++] = ',';
            output[pos++] = ' ';
        }

        if (pos + 1 < capacity)
            output[pos++] = letter;
    }

    output[pos] = '\0';
}

static int send_current_state(int player_id)
{
    char visible[MAX_WORD];
    char wrong[MAX_LINE];

    make_visible_word(&room.players[player_id].word_to_guess, visible, sizeof(visible));
    make_wrong_letters(&room.players[player_id].word_to_guess, wrong, sizeof(wrong));

    if (write_line_to_socket(room.players[player_id].fd, "WORD %s", visible) < 0)
        return -1;
    if (write_line_to_socket(room.players[player_id].fd, "WRONG %s", wrong) < 0)
        return -1;

    return 0;
}

static void mark_finished_and_wait(int player_id)
{
    pthread_mutex_lock(&room.mutex);

    room.solved[player_id] = 1;
    pthread_cond_broadcast(&room.finished_cond);

    while (!room.solved[0] || !room.solved[1])
        pthread_cond_wait(&room.finished_cond, &room.mutex);

    pthread_mutex_unlock(&room.mutex);
}

static void send_final_result(int player_id)
{
    int opponent_id = 1 - player_id;
    int my_misses = (int)secret_word_incorrect_guess_count(&room.players[player_id].word_to_guess);
    int opponent_misses = (int)secret_word_incorrect_guess_count(&room.players[opponent_id].word_to_guess);

    const char *result = "TIE";
    if (my_misses < opponent_misses)
        result = "WIN";
    else if (my_misses > opponent_misses)
        result = "LOSE";

    char my_wrong[MAX_LINE];
    char opponent_wrong[MAX_LINE];
    make_wrong_letters(&room.players[player_id].word_to_guess, my_wrong, sizeof(my_wrong));
    make_wrong_letters(&room.players[opponent_id].word_to_guess, opponent_wrong, sizeof(opponent_wrong));

    write_line_to_socket(room.players[player_id].fd, "FINAL %s", result);
    write_line_to_socket(room.players[player_id].fd, "YOUR %s", my_wrong);
    write_line_to_socket(room.players[player_id].fd, "OPP %s", opponent_wrong);
}

static void *play_for_one_client(void *arg)
{
    int player_id = (int)(intptr_t)arg;
    int fd = room.players[player_id].fd;
    char line[MAX_LINE];

    if (send_current_state(player_id) < 0)
        goto done;

    while (!secret_word_is_solved(&room.players[player_id].word_to_guess)) {
        if (read_line_from_socket(fd, line, sizeof(line)) < 0)
            goto done;

        if (line[0] != '\0')
            secret_word_guess(&room.players[player_id].word_to_guess, line[0]);

        if (send_current_state(player_id) < 0)
            goto done;
    }

    write_line_to_socket(fd, "SOLVED");

done:
    mark_finished_and_wait(player_id);
    send_final_result(player_id);
    close(fd);
    return NULL;
}

static int create_server_socket(int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    int value = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    if (listen(fd, PLAYERS) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

static int receive_valid_word(int fd, char *word, size_t capacity)
{
    if (read_line_from_socket(fd, word, capacity) < 0) {
        write_line_to_socket(fd, "ERR connection error");
        return -1;
    }

    secret_word_t test_word;
    if (!secret_word_init_from_c_string(&test_word, word)) {
        write_line_to_socket(fd, "ERR invalid word");
        return -1;
    }

    secret_word_free(&test_word);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);

    int port = atoi(argv[1]);
    int server_fd = create_server_socket(port);
    if (server_fd < 0)
        return 1;

    pthread_mutex_init(&room.mutex, NULL);
    pthread_cond_init(&room.finished_cond, NULL);
    room.solved[0] = 0;
    room.solved[1] = 0;

    printf("Listening on %d...\n", port);
    fflush(stdout);

    char submitted_words[PLAYERS][MAX_WORD];
    int accepted = 0;

    while (accepted < PLAYERS) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            perror("accept");
            close(server_fd);
            return 1;
        }

        if (receive_valid_word(client_fd, submitted_words[accepted], sizeof(submitted_words[accepted])) < 0) {
            close(client_fd);
            continue;
        }

        room.players[accepted].fd = client_fd;
        accepted++;
    }

    close(server_fd);

    secret_word_init_from_c_string(&room.players[0].word_to_guess, submitted_words[1]);
    secret_word_init_from_c_string(&room.players[1].word_to_guess, submitted_words[0]);

    write_line_to_socket(room.players[0].fd, "OK");
    write_line_to_socket(room.players[1].fd, "OK");

    pthread_t threads[PLAYERS];
    pthread_create(&threads[0], NULL, play_for_one_client, (void *)(intptr_t)0);
    pthread_create(&threads[1], NULL, play_for_one_client, (void *)(intptr_t)1);

    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);

    secret_word_free(&room.players[0].word_to_guess);
    secret_word_free(&room.players[1].word_to_guess);
    pthread_cond_destroy(&room.finished_cond);
    pthread_mutex_destroy(&room.mutex);

    return 0;
}
