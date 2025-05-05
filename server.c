#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#define PORT 21
#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024

typedef struct {
    int socket;
    int authenticated;
    char username[50];
    char client_ip[INET_ADDRSTRLEN];
    int client_data_port;
} Client;

Client clients[MAX_CLIENTS];
int client_count = 0;

int authenticate_user(const char* user, const char* pass) {
    FILE* fp = fopen("users.txt", "r");
    if (!fp) return 0;

    char u[100], p[100];
    while (fscanf(fp, "%s %s", u, p) != EOF) {
        if (strcmp(u, user) == 0 && strcmp(p, pass) == 0) {
            fclose(fp);
            return 1;
        }
    }

    fclose(fp);
    return 0;
}

void send_response(int sockfd, const char* msg) {
    send(sockfd, msg, strlen(msg), 0);
}

void handle_retr(Client* client, const char* filename) {
    send_response(client->socket, "150 File status okay; about to open data connection.\r\n");

    int data_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client->client_data_port);
    inet_pton(AF_INET, client->client_ip, &client_addr.sin_addr);

    sleep(1);  // give client time to listen
    if (connect(data_sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        perror("Data connection failed");
        send_response(client->socket, "425 Can't open data connection.\r\n");
        return;
    }

    FILE* file = fopen(filename, "rb");
    if (!file) {
        send_response(client->socket, "550 No such file or directory.\r\n");
        close(data_sock);
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        send(data_sock, buffer, bytes, 0);
    }

    fclose(file);
    close(data_sock);
    send_response(client->socket, "226 Transfer completed.\r\n");
}

void handle_stor(Client* client, const char* filename) {
    send_response(client->socket, "150 File status okay; about to open data connection.\r\n");

    int data_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client->client_data_port);
    inet_pton(AF_INET, client->client_ip, &client_addr.sin_addr);

    sleep(1);
    if (connect(data_sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        perror("Data connection failed");
        send_response(client->socket, "425 Can't open data connection.\r\n");
        return;
    }

    FILE* file = fopen(filename, "wb");
    if (!file) {
        send_response(client->socket, "550 Cannot create file.\r\n");
        close(data_sock);
        return;
    }

    char buffer[BUFFER_SIZE];
    ssize_t bytes;
    while ((bytes = recv(data_sock, buffer, BUFFER_SIZE, 0)) > 0) {
        fwrite(buffer, 1, bytes, file);
    }

    fclose(file);
    close(data_sock);
    send_response(client->socket, "226 Transfer completed.\r\n");
}

void handle_list(Client* client) {
    send_response(client->socket, "150 File status okay; about to open data connection.\r\n");

    int data_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(client->client_data_port);
    inet_pton(AF_INET, client->client_ip, &client_addr.sin_addr);

    sleep(1);
    if (connect(data_sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        perror("Data connection failed");
        send_response(client->socket, "425 Can't open data connection.\r\n");
        return;
    }

    DIR* dir = opendir(".");
    struct dirent* entry;
    char buffer[BUFFER_SIZE];
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_REG) {
            snprintf(buffer, sizeof(buffer), "%s\n", entry->d_name);
            send(data_sock, buffer, strlen(buffer), 0);
        }
    }

    closedir(dir);
    close(data_sock);
    send_response(client->socket, "226 Transfer completed.\r\n");
}

void handle_command(Client* client, char* cmd_line) {
    char cmd[5], arg[BUFFER_SIZE];
    sscanf(cmd_line, "%s %[^\r\n]", cmd, arg);

    if (strcasecmp(cmd, "USER") == 0) {
        strcpy(client->username, arg);
        send_response(client->socket, "331 Username OK, need password.\r\n");
    }
    else if (strcasecmp(cmd, "PASS") == 0) {
        if (authenticate_user(client->username, arg)) {
            client->authenticated = 1;
            send_response(client->socket, "230 User logged in, proceed.\r\n");
        } else {
            send_response(client->socket, "530 Not logged in.\r\n");
        }
    }
    else if (!client->authenticated) {
        send_response(client->socket, "530 Not logged in.\r\n");
    }
    else if (strcasecmp(cmd, "PORT") == 0) {
        int h1, h2, h3, h4, p1, p2;
        sscanf(arg, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2);
        sprintf(client->client_ip, "%d.%d.%d.%d", h1, h2, h3, h4);
        client->client_data_port = (p1 * 256) + p2;
        send_response(client->socket, "200 PORT command successful.\r\n");
    }
    else if (strcasecmp(cmd, "RETR") == 0) {
        handle_retr(client, arg);
    }
    else if (strcasecmp(cmd, "STOR") == 0) {
        handle_stor(client, arg);
    }
    else if (strcasecmp(cmd, "LIST") == 0) {
        handle_list(client);
    }
    else if (strcasecmp(cmd, "QUIT") == 0) {
        send_response(client->socket, "221 Service closing control connection.\r\n");
        close(client->socket);
        client->socket = 0;
    }
    else {
        send_response(client->socket, "202 Command not implemented.\r\n");
    }
}

int main() {
    int server_fd, new_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addrlen = sizeof(client_addr);
    char buffer[BUFFER_SIZE];

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr));
    listen(server_fd, 5);

    printf("FTP Server listening on port %d...\n", PORT);

    while (1) {
        new_socket = accept(server_fd, (struct sockaddr*)&client_addr, &addrlen);
        if (new_socket < 0) continue;

        if (client_count >= MAX_CLIENTS) {
            close(new_socket);
            continue;
        }

        Client* client = &clients[client_count++];
        client->socket = new_socket;
        client->authenticated = 0;
        memset(client->username, 0, sizeof(client->username));
        memset(client->client_ip, 0, sizeof(client->client_ip));
        client->client_data_port = 0;

        send_response(new_socket, "220 Service ready for new user.\r\n");

        while (1) {
            memset(buffer, 0, BUFFER_SIZE);
            int bytes = recv(new_socket, buffer, BUFFER_SIZE - 1, 0);
            if (bytes <= 0) break;

            handle_command(client, buffer);
        }

        close(new_socket);
        client->socket = 0;
        client_count--;
    }

    return 0;
}
