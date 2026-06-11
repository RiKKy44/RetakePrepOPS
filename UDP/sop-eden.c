#include "l8_common.h"

void usage(char* name)
{
    printf("%s <in_port>\n", name);
    printf("  in_port - port that accepts messages\n");
    exit(EXIT_FAILURE);
}

int is_valid_login(const char* login) {
    for (int i = 0; i < USERS; i++) {
        // Loginy w strukturze mają dokładnie 16 bajtów dopełnionych zerami
        if (strncmp(login, LOGINS[i], 16) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc != 2) {
        usage(argv[0]);
    }

    uint16_t port = (uint16_t)atoi(argv[1]);

    // Korzystamy z gotowej funkcji z l8_common.h!
    // Tworzy socket UDP (SOCK_DGRAM) i binduje go do podanego portu
    int socket_fd = bind_inet_socket(port, SOCK_DGRAM, 0);

    printf("EDEN Cluster Server listening on port %d...\n", port);

    char buffer[MSG_MAX];
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    int keep_running = 1;

    while (keep_running) {
        // Odbieramy datagram UDP
        ssize_t bytes_received = recvfrom(socket_fd, buffer, MSG_MAX, 0,
                                          (struct sockaddr*)&client_addr, &client_len);

        if (bytes_received < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            continue;
        }

        // WALIDACJA 1: Czy wiadomość ma przynajmniej rozmiar nagłówka (24 bajty)?
        if (bytes_received < 24) {
            fprintf(stderr, "error: wrong message length %ld\n", bytes_received);
            continue;
        }

        // Bezpieczne wyciągnięcie loginu (16 bajtów + \0 na końcu dla printf)
        char safe_login[17];
        memcpy(safe_login, buffer, 16);
        safe_login[16] = '\0';

        // Bezpieczne wyciągnięcie komendy (8 bajtów + \0 na końcu dla printf)
        char safe_cmd[9];
        memcpy(safe_cmd, buffer + 16, 8);
        safe_cmd[8] = '\0';

        if (!is_valid_login(safe_login)) {
            fprintf(stderr, "error: unknown user %s\n", safe_login);
            continue;
        }

        if (strcmp(safe_cmd, "RUN") == 0 || strcmp(safe_cmd, "EXIT") == 0 ||
            strcmp(safe_cmd, "PAUSE") == 0 || strcmp(safe_cmd, "LIST") == 0 ||
            strcmp(safe_cmd, "GATHER") == 0) {

            // Te komendy muszą mieć DOKŁADNIE 24 bajty (tylko nagłówek)
            if (bytes_received != 24) {
                fprintf(stderr, "error: wrong message length %ld\n", bytes_received);
                continue;
            }
            printf("%s: %s\n", safe_login, safe_cmd);

            if (strcmp(safe_cmd, "EXIT") == 0) {
                keep_running = 0;
            }

        }
        else if (strcmp(safe_cmd, "COMPUTE") == 0) {
            ssize_t param_bytes = bytes_received - 24;

            // Parametry nie mogą być puste i muszą składać się z par uint32_t (wielokrotność 8 bajtów)
            if (param_bytes == 0 || (param_bytes % 8) != 0) {
                fprintf(stderr, "error: wrong message length %ld\n", bytes_received);
                continue;
            }

            printf("%s: %s", safe_login, safe_cmd);

            //jest 8 par
            int pairs_count = param_bytes / 8;
            char* param_ptr = buffer + 24;

            for (int i = 0; i < pairs_count; i++) {
                uint32_t first_net, second_net;

                // Kopiujemy bezpiecznie po 4 bajty (zabezpieczenie przed unaligned memory access)
                memcpy(&first_net, param_ptr + (i * 8), 4);
                memcpy(&second_net, param_ptr + (i * 8) + 4, 4);

                // Konwersja z Network Byte Order na format procesora czyli z big endian to little endian
                uint32_t first_host = ntohl(first_net);
                uint32_t second_host = ntohl(second_net);

                printf(" (%u, %u)", first_host, second_host);
            }
            printf("\n");

        } else {
            fprintf(stderr, "error: unknown command %s\n", safe_cmd);
            continue;
        }
    }

    close(socket_fd);
    return EXIT_SUCCESS;
}
