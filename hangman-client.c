#include <arpa/inet.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_LINE 512

static int server_fd = -1;

static int read_line_from_server(char *buffer, size_t capacity)
{
    size_t pos = 0;

    while (1) {
        char ch = '\0';
        ssize_t bytes = recv(server_fd, &ch, 1, 0);
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

static int send_line_to_server(const char *text)
{
    char buffer[MAX_LINE];
    int length = snprintf(buffer, sizeof(buffer), "%s\n", text);

    if (length <= 0 || length >= (int)sizeof(buffer))
        return -1;

    ssize_t sent = send(server_fd, buffer, (size_t)length, MSG_NOSIGNAL);
    return sent == length ? 0 : -1;
}

static int connect_to_server(const char *host, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);

    if (inet_pton(AF_INET, host, &address.sin_addr) <= 0) {
        close(fd);
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

static void print_result(const char *result, const char *my_wrong, const char *opponent_wrong)
{
    if (strcmp(result, "WIN") == 0)
        printf("YOU WIN! :)\n");
    else if (strcmp(result, "LOSE") == 0)
        printf("You Lose! :(\n");
    else
        printf("Tie :/\n");

    printf("Your incorrect guesses: %s\n", my_wrong);
    printf("Opponent's incorrect guesses: %s\n", opponent_wrong);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <host> <port> <opponent-word>\n", argv[0]);
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);
    setvbuf(stdout, NULL, _IONBF, 0);

    server_fd = connect_to_server(argv[1], atoi(argv[2]));
    if (server_fd < 0) {
        perror("connect");
        return 1;
    }

    if (send_line_to_server(argv[3]) < 0) {
        fprintf(stderr, "Could not send word.\n");
        close(server_fd);
        return 1;
    }

    char line[MAX_LINE];
    if (read_line_from_server(line, sizeof(line)) < 0) {
        fprintf(stderr, "Server closed the connection.\n");
        close(server_fd);
        return 1;
    }

    if (strcmp(line, "OK") != 0) {
        fprintf(stderr, "%s\n", line);
        close(server_fd);
        return 1;
    }

    char visible_word[MAX_LINE] = "";
    char wrong_letters[MAX_LINE] = "";

    while (1) {
        if (read_line_from_server(line, sizeof(line)) < 0)
            break;

        if (strcmp(line, "SOLVED") == 0)
            continue;

        if (strncmp(line, "WORD ", 5) == 0)
            snprintf(visible_word, sizeof(visible_word), "%s", line + 5);

        if (read_line_from_server(line, sizeof(line)) < 0)
            break;

        if (strncmp(line, "WRONG ", 6) == 0)
            snprintf(wrong_letters, sizeof(wrong_letters), "%s", line + 6);

        printf("Word: %s\n", visible_word);
        printf("Incorrect guesses: %s\n", wrong_letters);

        if (strchr(visible_word, '_') == NULL)
            break;

        char input[MAX_LINE];
        if (fgets(input, sizeof(input), stdin) == NULL)
            break;

        char guess[2] = { input[0], '\0' };
        send_line_to_server(guess);
    }

    char final_message[32] = "TIE";
    char my_wrong[MAX_LINE] = "";
    char opponent_wrong[MAX_LINE] = "";
    int received = 0;

    while (received != 7 && read_line_from_server(line, sizeof(line)) >= 0) {
        if (strcmp(line, "SOLVED") == 0) {
            continue;
        } else if (strncmp(line, "FINAL ", 6) == 0) {
            snprintf(final_message, sizeof(final_message), "%s", line + 6);
            received |= 1;
        } else if (strncmp(line, "YOUR ", 5) == 0) {
            snprintf(my_wrong, sizeof(my_wrong), "%s", line + 5);
            received |= 2;
        } else if (strncmp(line, "OPP ", 4) == 0) {
            snprintf(opponent_wrong, sizeof(opponent_wrong), "%s", line + 4);
            received |= 4;
        }
    }

    print_result(final_message, my_wrong, opponent_wrong);
    close(server_fd);
    return 0;
}
