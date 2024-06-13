#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define SERVER_IP "127.0.0.1"
#define PORT 8081
#define BUFFER_SIZE 4096

typedef struct {
    int sock;
    char file_type[10];
    char file_path[256];
    char format[10];
} client_args;

void *send_file(void *args);
void *receive_url(void *args);

void read_prompt(int sock, char *prompt_buffer) {
    int read_size = recv(sock, prompt_buffer, BUFFER_SIZE - 1, 0);
    if (read_size > 0) {
        prompt_buffer[read_size] = '\0';
        printf("%s", prompt_buffer);
    }
}

int main(int argc, char *argv[]) {
    int sock;
    struct sockaddr_in server;
    client_args *args;

    args = (client_args*)malloc(sizeof(client_args));

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Could not create socket");
        free(args);
        return 1;
    }
    puts("Socket created");

    server.sin_addr.s_addr = inet_addr(SERVER_IP);
    server.sin_family = AF_INET;
    server.sin_port = htons(PORT);

    // Connect to remote server
    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Connect failed");
        close(sock);
        free(args);
        return 1;
    }
    puts("Connected to server");

    args->sock = sock;

    // Read and print server's prompt for file type
    read_prompt(sock, (char*)malloc(BUFFER_SIZE));

    // Get user input for file type
    scanf("%9s", args->file_type);

    // Send the file type
    send(sock, args->file_type, strlen(args->file_type) + 1, 0);

    // Read and print server's prompt for file path
    read_prompt(sock, (char*)malloc(BUFFER_SIZE));

    // Get user input for file path
    scanf("%255s", args->file_path);

    // Send the file path
    send(sock, args->file_path, strlen(args->file_path) + 1, 0);

    // Read and print server's prompt for format
    read_prompt(sock, (char*)malloc(BUFFER_SIZE));

    // Get user input for format
    scanf("%9s", args->format);

    pthread_t send_thread, receive_thread;
    if (pthread_create(&send_thread, NULL, send_file, (void*)args) < 0) {
        perror("Could not create send thread");
        close(sock);
        free(args);
        return 1;
    }

    if (pthread_create(&receive_thread, NULL, receive_url, (void*)args) < 0) {
        perror("Could not create receive thread");
        close(sock);
        free(args);
        return 1;
    }

    pthread_join(send_thread, NULL);
    pthread_join(receive_thread, NULL);

    close(sock);
    free(args);
    return 0;
}

void *send_file(void *args) {
    client_args *client = (client_args*)args;
    char buffer[BUFFER_SIZE];
    int read_size;
    FILE *file;

    // Send the format to the server
    send(client->sock, client->format, strlen(client->format) + 1, 0);

    // Open and send the file
    file = fopen(client->file_path, "rb");
    if (!file) {
        perror("Failed to open file");
        close(client->sock);
        return NULL;
    }

    while ((read_size = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
        send(client->sock, buffer, read_size, 0);
    }
    fclose(file);
    shutdown(client->sock, SHUT_WR);  // Signal that sending is complete

    return NULL;
}

void *receive_url(void *args) {
    client_args *client = (client_args*)args;
    char buffer[BUFFER_SIZE];
    int read_size;

    // Receive the download URL from the server
    if ((read_size = recv(client->sock, buffer, BUFFER_SIZE, 0)) > 0) {
        buffer[read_size] = '\0';  // Null-terminate the string
        printf("Download the converted file from: %s\n", buffer);
    } else if (read_size == -1) {
        perror("Recv failed");
    }

    return NULL;
}
