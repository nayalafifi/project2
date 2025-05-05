/*
FTP client implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>

#define BUFFER_SIZE 1024
#define SERVER_PORT 21 //server port 
#define CLIENT_PORT_START 1024 // Base port for data connections
#define MAX_CMD_LENGTH 512

// Function prototypes
int connect_to_server(const char* server_address);
int login_to_server(int control_socket);
void process_user_commands(int control_socket);
int handle_data_transfer(int control_socket, const char* command, const char* filename);
int send_command(int socket, const char* command);
int receive_response(int socket, char* response, int response_size);
void execute_local_command(const char* command);
int create_data_socket(int control_socket, int* data_port);

int main(int argc, char* argv[]) 
{
    int control_socket;
    char server_address[100];
    
    if (argc != 2) //checking the command line argument 
    {
        printf("Usage: %s <server_address>\n", argv[0]);
        return 1;
    }
    
    strcpy(server_address, argv[1]);
    
    // Connect to server
    control_socket = connect_to_server(server_address);//connecting to the server
    if (control_socket < 0) 
    {
        printf("Failed to connect to the server.\n");
        return 1;
    }
    
    if (login_to_server(control_socket) < 0) 
    {
        printf("Login failed.\n");
        close(control_socket);
        return 1;
    }
    
    process_user_commands(control_socket);//process the user commands
    
    return 0;
}


int connect_to_server(const char* server_address) //function to establish a conenction w the server 
{
    int sockfd;
    struct sockaddr_in server_addr;
    char response[BUFFER_SIZE];
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);//create socket as we do usually 
    if (sockfd < 0) 
    {
        perror("Error creating socket");
        return -1;
    }
    
    memset(&server_addr, 0, sizeof(server_addr));//set up the server address 
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    if (inet_pton(AF_INET, server_address, &server_addr.sin_addr) <= 0) 
    {
        perror("Invalid address");
        close(sockfd);
        return -1;
    }
    
    // Connect to server
    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) 
    {
        perror("Connection failed");
        close(sockfd);
        return -1;
    }
    
    // Receive welcome message
    if (receive_response(sockfd, response, BUFFER_SIZE) <= 0) 
    {
        printf("No response from server\n");
        close(sockfd);
        return -1;
    }
    
    printf("%s", response);
    return sockfd;
}

// Function to handle login to the server
int login_to_server(int control_socket) 
{
    char username[100];
    char password[100];
    char command[MAX_CMD_LENGTH];
    char response[BUFFER_SIZE];
    
    // Get username
    printf("Username: ");
    fgets(username, sizeof(username), stdin);
    username[strcspn(username, "\n")] = '\0'; // Remove newline
    
    // Send USER command
    snprintf(command, sizeof(command), "USER %s\r\n", username);
    if (send_command(control_socket, command) < 0) 
    {
        return -1;
    }
    
    // Get response
    if (receive_response(control_socket, response, BUFFER_SIZE) <= 0) 
    {
        return -1;
    }
    printf("%s", response);
    
    // Check if username is accepted
    if (strncmp(response, "331", 3) != 0) 
    {
        return -1;
    }
    
    // Get password
    printf("Password: ");
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = '\0'; // Remove newline
    
    // Send PASS command
    snprintf(command, sizeof(command), "PASS %s\r\n", password);
    if (send_command(control_socket, command) < 0) 
    {
        return -1;
    }
    
    // Get response
    if (receive_response(control_socket, response, BUFFER_SIZE) <= 0) 
    {
        return -1;
    }
    printf("%s", response);
    
    // Check if login successful
    if (strncmp(response, "230", 3) != 0) 
    {
        return -1;
    }
    
    return 0;
}

// Function to process user commands
void process_user_commands(int control_socket) 
{
    char input[MAX_CMD_LENGTH];
    char command[MAX_CMD_LENGTH];
    char response[BUFFER_SIZE];
    int running = 1;
    
    while (running) {
        printf("ftp> ");
        fflush(stdout);
        
        if (fgets(input, sizeof(input), stdin) == NULL) 
        {
            break;
        }
        
        // Remove newline
        input[strcspn(input, "\n")] = '\0';
        
        // Empty command
        if (strlen(input) == 0) 
        {
            continue;
        }
        
        // Check if it's a local command (starts with !)
        if (input[0] == '!') 
        {
            execute_local_command(input + 1);
            continue;
        }
        
        // Parse command and arguments
        char cmd[MAX_CMD_LENGTH];
        char arg[MAX_CMD_LENGTH];
        arg[0] = '\0'; // Initialize arg as empty string
        
        // Split input into command and argument
        sscanf(input, "%s %s", cmd, arg);
        
        // Handle QUIT command
        if (strcmp(cmd, "QUIT") == 0) 
        {
            sprintf(command, "QUIT\r\n");
            send_command(control_socket, command);
            receive_response(control_socket, response, BUFFER_SIZE);
            printf("%s", response);
            running = 0;
            continue;
        }
        
        // Handle data transfer commands (RETR, STOR, LIST)
        if (strcmp(cmd, "RETR") == 0 || strcmp(cmd, "STOR") == 0 || strcmp(cmd, "LIST") == 0) {
            handle_data_transfer(control_socket, cmd, arg);
            continue;
        }
        
        // Handle regular commands
        sprintf(command, "%s %s\r\n", cmd, arg);
        if (send_command(control_socket, command) < 0) {
            printf("Failed to send command\n");
            continue;
        }
        
        if (receive_response(control_socket, response, BUFFER_SIZE) <= 0) {
            printf("No response from server\n");
            continue;
        }
        
        printf("%s", response);
    }
    
    close(control_socket);
}

// Function to handle data transfer commands (RETR, STOR, LIST)
int handle_data_transfer(int control_socket, const char* command, const char* filename) {
    int data_port;
    int data_socket = create_data_socket(control_socket, &data_port);
    char cmd[MAX_CMD_LENGTH];
    char response[BUFFER_SIZE];
    
    if (data_socket < 0) {
        return -1;
    }
    
    // Send the actual command
    if (strcmp(command, "LIST") == 0) {
        sprintf(cmd, "LIST\r\n");
    } else {
        sprintf(cmd, "%s %s\r\n", command, filename);
    }
    
    if (send_command(control_socket, cmd) < 0) {
        close(data_socket);
        return -1;
    }
    
    // Get response to the command
    if (receive_response(control_socket, response, BUFFER_SIZE) <= 0) {
        close(data_socket);
        return -1;
    }
    printf("%s", response);
    
    // Check if response indicates data transfer will start
    if (strncmp(response, "150", 3) != 0) {
        close(data_socket);
        return -1;
    }
    
    // Accept connection from server for data transfer
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int conn_socket = accept(data_socket, (struct sockaddr*)&client_addr, &client_len);
    
    if (conn_socket < 0) {
        perror("Accept failed");
        close(data_socket);
        return -1;
    }
    
    // Perform data transfer based on command
    if (strcmp(command, "RETR") == 0) {
        // File download (RETR)
        FILE* file = fopen(filename, "wb");
        if (!file) {
            perror("Failed to open local file for writing");
            close(conn_socket);
            close(data_socket);
            return -1;
        }

        char buffer[BUFFER_SIZE];
        int bytes_read;
        while ((bytes_read = read(conn_socket, buffer, BUFFER_SIZE)) > 0) {
            if (fwrite(buffer, 1, bytes_read, file) != bytes_read) {
                perror("Failed to write to local file");
                fclose(file);
                close(conn_socket);
                close(data_socket);
                return -1;
            }
        }

        fclose(file);
        if (bytes_read < 0) {
            perror("Read error on data connection");
            close(conn_socket);
            close(data_socket);
            return -1;
        }
        printf("File download completed successfully.\n");
    } else if (strcmp(command, "STOR") == 0) {
        // File upload (STOR)
        FILE* file = fopen(filename, "rb");
        if (!file) {
            perror("Failed to open local file for reading");
            close(conn_socket);
            close(data_socket);
            return -1;
        }

        char buffer[BUFFER_SIZE];
        int bytes_read;
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
            if (write(conn_socket, buffer, bytes_read) != bytes_read) {
                perror("Failed to send file data");
                fclose(file);
                close(conn_socket);
                close(data_socket);
                return -1;
            }
        }

        fclose(file);
        if (ferror(file)) {
            perror("Error reading local file");
            close(conn_socket);
            close(data_socket);
            return -1;
        }
        printf("File upload completed successfully.\n");
    } else if (strcmp(command, "LIST") == 0) {
        // Directory listing (LIST)
        char buffer[BUFFER_SIZE];
        int bytes_read;
        
        while ((bytes_read = read(conn_socket, buffer, BUFFER_SIZE - 1)) > 0) {
            buffer[bytes_read] = '\0';
            printf("%s", buffer);
        }
    }
    
    // Close data connections
    close(conn_socket);
    close(data_socket);
    
    // Get response after data transfer
    if (receive_response(control_socket, response, BUFFER_SIZE) <= 0) {
        return -1;
    }
    printf("%s", response);
    
    return 0;
}

// Function to create a data socket for file transfers
int create_data_socket(int control_socket, int* data_port) 
{
    int data_sock;
    struct sockaddr_in data_addr;
    char port_cmd[MAX_CMD_LENGTH];
    char response[BUFFER_SIZE];
    
    // Create data socket
    data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (data_sock < 0) {
        perror("Error creating data socket");
        return -1;
    }
    
    // Setup data socket address
    memset(&data_addr, 0, sizeof(data_addr));
    data_addr.sin_family = AF_INET;
    data_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    // Try to bind to a port (try different ports if needed)
    *data_port = CLIENT_PORT_START;
    int max_attempts = 100;
    
    for (int i = 0; i < max_attempts; i++) {
        data_addr.sin_port = htons(*data_port);
        
        if (bind(data_sock, (struct sockaddr*)&data_addr, sizeof(data_addr)) == 0) {
            break;
        }
        
        (*data_port)++;
        
        if (i == max_attempts - 1) {
            perror("Failed to bind data socket");
            close(data_sock);
            return -1;
        }
    }
    
    // Listen for connection
    if (listen(data_sock, 1) < 0) {
        perror("Listen failed");
        close(data_sock);
        return -1;
    }
    
    // Get local IP address
    struct sockaddr_in local_addr;
    socklen_t addr_len = sizeof(local_addr);
    getsockname(control_socket, (struct sockaddr*)&local_addr, &addr_len);
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &local_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
    
    // Replace dots with commas in IP address
    for (int i = 0; i < strlen(ip_str); i++) {
        if (ip_str[i] == '.') {
            ip_str[i] = ',';
        }
    }
    
    // Calculate p1 and p2 from port
    int p1 = *data_port / 256;
    int p2 = *data_port % 256;
    
    // Send PORT command
    sprintf(port_cmd, "PORT %s,%d,%d\r\n", ip_str, p1, p2);
    if (send_command(control_socket, port_cmd) < 0) 
    {
        close(data_sock);
        return -1;
    }
    
    // Get response
    if (receive_response(control_socket, response, BUFFER_SIZE) <= 0) 
    {
        close(data_sock);
        return -1;
    }
    printf("%s", response);
    
    // Check if PORT command successful
    if (strncmp(response, "200", 3) != 0) 
    {
        close(data_sock);
        return -1;
    }
    
    return data_sock;
}

// Function to send a command to the server
int send_command(int socket, const char* command) 
{
    if (send(socket, command, strlen(command), 0) < 0) 
    {
        perror("Send failed");
        return -1;
    }
    return 0;
}

// Function to receive a response from the server
int receive_response(int socket, char* response, int response_size) {
    int total_received = 0;
    int bytes_received;
    
    memset(response, 0, response_size);
    
    while ((bytes_received = recv(socket, response + total_received, 
                                 response_size - total_received - 1, 0)) > 0) {
        total_received += bytes_received;
        response[total_received] = '\0';
        
        // Check if we have received a complete response
        if (total_received >= 4 && 
            response[total_received-1] == '\n' &&
            response[total_received-2] == '\r') {
            break;
        }
    }
    
    if (bytes_received < 0) {
        perror("Receive failed");
        return -1;
    }
    
    return total_received;
}

// Function to execute local commands (commands starting with !)
void execute_local_command(const char* command) {
    if (strcmp(command, "LIST") == 0) {
        system("ls");
    } else if (strncmp(command, "CWD", 3) == 0) {
        // Extract directory name
        const char* dir = command + 4;
        if (chdir(dir) != 0) {
            perror("Local directory change failed");
        }
    } else if (strcmp(command, "PWD") == 0) {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            printf("Local directory: %s\n", cwd);
        } else {
            perror("getcwd() error");
        }
    } else {
        printf("Unknown local command: %s\n", command);
    }
}