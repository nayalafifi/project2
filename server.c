#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define PORT 21
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

typedef struct {
    int sockfd;
    int authenticated;
    char username[100];
    char client_ip[INET_ADDRSTRLEN];
    int client_data_port;
    char cwd[1024];
} Client;

Client clients[MAX_CLIENTS];

void trim(char *str) {
    str[strcspn(str, "\r\n")] = 0;
}

void send_response(int sockfd, const char *msg) {
    send(sockfd, msg, strlen(msg), 0);
}

int authenticate(const char *username, const char *password) {
    FILE *fp = fopen("users.txt", "r");
    if (!fp) return 0;
    char u[100], p[100];
    while (fscanf(fp, "%s %s", u, p) != EOF) {
        if (strcmp(username, u) == 0 && strcmp(password, p) == 0) {
            fclose(fp);
            return 1;
        }
    }
    fclose(fp);
    return 0;
}

void parse_port(char *arg, char *ip, int *port) {
    int h1, h2, h3, h4, p1, p2;
    sscanf(arg, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2);
    sprintf(ip, "%d.%d.%d.%d", h1, h2, h3, h4);
    *port = (p1 * 256) + p2;
    printf("Port received: %s,%d\n", ip, *port);
}

void handle_data_connection(Client *client, const char *command, const char *arg) {
    printf("File okay, beginning data connections\n");
    printf("Connecting to Client Transfer Socket...\n");

    int data_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client->client_data_port);
    inet_pton(AF_INET, client->client_ip, &client_addr.sin_addr);

    sleep(1);
    if (connect(data_sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        send_response(client->sockfd, "425 Can't open data connection.\r\n");
        return;
    }

    printf("Connection Successful\n");

    if (strcmp(command, "LIST") == 0) {
        printf("Listing directory\n");
        send_response(client->sockfd, "150 Opening data connection.\r\n");
        DIR *dir = opendir(client->cwd);
        struct dirent *entry;
        char buffer[BUFFER_SIZE];
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_type == DT_REG) {
                snprintf(buffer, sizeof(buffer), "%s\n", entry->d_name);
                send(data_sock, buffer, strlen(buffer), 0);
            }
        }
        closedir(dir);
    } else if (strcmp(command, "RETR") == 0) {
        send_response(client->sockfd, "150 Opening data connection.\r\n");
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", client->cwd, arg);
        FILE *f = fopen(path, "rb");
        if (!f) {
            send_response(client->sockfd, "550 File not found.\r\n");
            close(data_sock);
            return;
        }
        char buffer[BUFFER_SIZE];
        size_t n;
        while ((n = fread(buffer, 1, BUFFER_SIZE, f)) > 0) {
            send(data_sock, buffer, n, 0);
        }
        fclose(f);
    } else if (strcmp(command, "STOR") == 0) {
        send_response(client->sockfd, "150 Opening data connection.\r\n");
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", client->cwd, arg);
        FILE *f = fopen(path, "wb");
        if (!f) {
            send_response(client->sockfd, "550 Cannot write file.\r\n");
            close(data_sock);
            return;
        }
        char buffer[BUFFER_SIZE];
        ssize_t n;
        while ((n = recv(data_sock, buffer, BUFFER_SIZE, 0)) > 0) {
            fwrite(buffer, 1, n, f);
        }
        fclose(f);
    }

    close(data_sock);
    send_response(client->sockfd, "226 Transfer complete.\r\n");
    printf("226 Transfer complete\n");
}

void process_command(Client *client, char *cmd_line) {
    char command[BUFFER_SIZE], arg[BUFFER_SIZE];
    memset(arg, 0, sizeof(arg));
    sscanf(cmd_line, "%s %[^\r\n]", command, arg);

    if (strcasecmp(command, "USER") == 0) {
        strcpy(client->username, arg);
        send_response(client->sockfd, "331 Username OK, need password.\r\n");
        printf("Successful username verification\n");
    } else if (strcasecmp(command, "PASS") == 0) {
        if (authenticate(client->username, arg)) {
            client->authenticated = 1;
            char user_dir[1024];
            snprintf(user_dir, sizeof(user_dir), "./%s", client->username);
            mkdir(user_dir, 0755);
            chdir(user_dir);
            getcwd(client->cwd, sizeof(client->cwd));
            send_response(client->sockfd, "230 User logged in, proceed.\r\n");
            printf("Successful login\n");
        } else {
            send_response(client->sockfd, "530 Not logged in.\r\n");
        }
    } else if (!client->authenticated) {
        send_response(client->sockfd, "530 Not logged in.\r\n");
    } else if (strcasecmp(command, "PORT") == 0) {
        parse_port(arg, client->client_ip, &client->client_data_port);
        send_response(client->sockfd, "200 PORT command successful.\r\n");
    } else if (strcasecmp(command, "PWD") == 0) {
        char resp[BUFFER_SIZE];
        snprintf(resp, sizeof(resp), "257 \"%s\"\r\n", client->cwd);
        send_response(client->sockfd, resp);
    } else if (strcasecmp(command, "CWD") == 0) {
        char new_dir[1024];
        snprintf(new_dir, sizeof(new_dir), "%s/%s", client->cwd, arg);
        if (chdir(new_dir) == 0) {
            getcwd(client->cwd, sizeof(client->cwd));
            char resp[BUFFER_SIZE];
            snprintf(resp, sizeof(resp), "200 directory changed to %s\r\n", client->cwd);
            send_response(client->sockfd, resp);
            printf("Changing directory to: %s\n", arg);
        } else {
            send_response(client->sockfd, "550 No such file or directory.\r\n");
        }
    } else if (strcasecmp(command, "RETR") == 0 ||
               strcasecmp(command, "STOR") == 0 ||
               strcasecmp(command, "LIST") == 0) {
        if (fork() == 0) {
            handle_data_connection(client, command, arg);
            exit(0);
        }
    } else if (strcasecmp(command, "QUIT") == 0) {
        send_response(client->sockfd, "221 Service closing control connection.\r\n");
        close(client->sockfd);
        client->sockfd = 0;
        printf("Closed!\n");
    } else {
        send_response(client->sockfd, "202 Command not implemented.\r\n");
    }
}

int main() {
    int server_fd, new_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, 5);
    printf("FTP Server started on port %d...\n", PORT);

    fd_set master_set, read_set;
    FD_ZERO(&master_set);
    FD_SET(server_fd, &master_set);
    int fdmax = server_fd;

    for (int i = 0; i < MAX_CLIENTS; i++) clients[i].sockfd = 0;

    while (1) {
        read_set = master_set;
        if (select(fdmax + 1, &read_set, NULL, NULL, NULL) < 0) continue;

        for (int i = 0; i <= fdmax; i++) {
            if (FD_ISSET(i, &read_set)) {
                if (i == server_fd) {
                    new_sock = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
                    for (int j = 0; j < MAX_CLIENTS; j++) {
                        if (clients[j].sockfd == 0) {
                            clients[j].sockfd = new_sock;
                            clients[j].authenticated = 0;
                            getcwd(clients[j].cwd, sizeof(clients[j].cwd));
                            FD_SET(new_sock, &master_set);
                            if (new_sock > fdmax) fdmax = new_sock;
                            send_response(new_sock, "220 Service ready for new user.\r\n");
                            printf("Connection established with user %d\n", j);
                            printf("Their port: %d\n", ntohs(client_addr.sin_port));
                            break;
                        }
                    }
                } else {
                    char buffer[BUFFER_SIZE];
                    int bytes = recv(i, buffer, sizeof(buffer) - 1, 0);
                    if (bytes <= 0) {
                        close(i);
                        FD_CLR(i, &master_set);
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (clients[j].sockfd == i) clients[j].sockfd = 0;
                        }
                    } else {
                        buffer[bytes] = '\0';
                        trim(buffer);
                        for (int j = 0; j < MAX_CLIENTS; j++) {
                            if (clients[j].sockfd == i) {
                                process_command(&clients[j], buffer);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    return 0;
}
