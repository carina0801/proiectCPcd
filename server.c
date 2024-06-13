#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 8081
#define BUFFER_SIZE 4096
#define TMP_DIR "/tmp/"
#define HTTP_SERVER_PORT 8082

void *handle_client(void *socket_desc);
void *start_http_server(void *arg);

int main() {
    int server_fd, client_sock, c;
    struct sockaddr_in server, client;
    pthread_t thread_id, http_thread_id;

    // Start the HTTP server in a separate thread
    if (pthread_create(&http_thread_id, NULL, start_http_server, NULL) < 0) {
        perror("Could not create HTTP server thread");
        return 1;
    }

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Could not create socket");
        return 1;
    }
    puts("Socket created");

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }
    puts("Bind done");

    listen(server_fd, 3);
    puts("Waiting for incoming connections...");
    c = sizeof(struct sockaddr_in);

    while ((client_sock = accept(server_fd, (struct sockaddr *)&client, (socklen_t *)&c))) {
        puts("Connection accepted");

        int *new_sock = malloc(sizeof(int));
        *new_sock = client_sock;

        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            perror("Could not create thread");
            free(new_sock);
            continue;
        }
        pthread_detach(thread_id);
    }

    if (client_sock < 0) {
        perror("Accept failed");
        close(server_fd);
        return 1;
    }

    close(server_fd);
    return 0;
}

void *handle_client(void *socket_desc) {
    int sock = *(int*)socket_desc;
    free(socket_desc);
    char file_path[256], output_format[10], file_type[10], client_message[BUFFER_SIZE], new_file_path[512];
    FILE *file;
    int read_size, total_bytes_received = 0;

    // Ask for file type (audio/video)
    const char *file_type_prompt = "Enter the type of file (audio/video): ";
    send(sock, file_type_prompt, strlen(file_type_prompt), 0);
    read_size = recv(sock, file_type, sizeof(file_type) - 1, 0);
    if (read_size <= 0) {
        perror("Failed to receive file type");
        close(sock);
        return NULL;
    }
    file_type[read_size] = '\0';

    // Ask for file path
    const char *file_path_prompt = "Enter the file path: ";
    send(sock, file_path_prompt, strlen(file_path_prompt), 0);
    read_size = recv(sock, file_path, sizeof(file_path) - 1, 0);
    if (read_size <= 0) {
        perror("Failed to receive file path");
        close(sock);
        return NULL;
    }
    file_path[read_size] = '\0';  // Null-terminate the string

    // Extract filename from the given path
    char *filename = strrchr(file_path, '/');
    if (filename == NULL) {
        filename = file_path;  // No '/' found, use the whole path as filename
    } else {
        filename++;  // Move past the '/'
    }

    // Construct a new path in /tmp
    snprintf(new_file_path, sizeof(new_file_path), "%s%s", TMP_DIR, filename);

    // Ask for output format
    const char *format_prompt = "Enter desired output format (e.g., mp3, ogg): ";
    send(sock, format_prompt, strlen(format_prompt), 0);
    read_size = recv(sock, output_format, sizeof(output_format) - 1, 0);
    if (read_size <= 0) {
        perror("Failed to receive format");
        close(sock);
        return NULL;
    }
    output_format[read_size] = '\0';

    printf("Received file type: %s\n", file_type);
    printf("Received file path: %s\n", new_file_path);
    printf("Received output format: %s\n", output_format);

    // Open the file for writing in /tmp
    file = fopen(new_file_path, "wb");
    if (!file) {
        perror("Failed to open file for writing");
        close(sock);
        return NULL;
    }

    while ((read_size = recv(sock, client_message, BUFFER_SIZE, 0)) > 0) {
        fwrite(client_message, sizeof(char), read_size, file);
        total_bytes_received += read_size;
    }
    fclose(file);

    if (read_size < 0) {
        perror("Receive failed");
        close(sock);
        return NULL;
    }

    printf("Total bytes received and written to file: %d\n", total_bytes_received);

    char command[2048];  // Increase buffer size to 2048
    char out_filename[512];  // Increase buffer size to 512

    // Construct the output filename
    char *dot = strrchr(filename, '.');
    if (dot) *dot = '\0';  // Remove the original extension
    snprintf(out_filename, sizeof(out_filename), "%s.%s", filename, output_format);

    if (strcmp(file_type, "video") == 0) {
        // Video file, extract audio
        snprintf(command, sizeof(command), "ffmpeg -i \"%s\" -q:a 0 -map a \"%s%s\"", new_file_path, TMP_DIR, out_filename);
    } else if (strcmp(file_type, "audio") == 0) {
        // Audio file, convert to desired format
        snprintf(command, sizeof(command), "ffmpeg -i \"%s\" \"%s%s\"", new_file_path, TMP_DIR, out_filename);
    } else {
        perror("Unsupported file format");
        close(sock);
        return NULL;
    }

    printf("Executing command: %s\n", command);
    if (system(command) != 0) {
        perror("FFmpeg command failed");
        close(sock);
        return NULL;
    }

    // Provide the download URL to the client
    char download_url[1024];  // Ensure buffer size is sufficient
    snprintf(download_url, sizeof(download_url), "http://127.0.0.1:%d/%s", HTTP_SERVER_PORT, out_filename);
    printf("Download URL: %s\n", download_url);
    send(sock, download_url, strlen(download_url), 0);

    close(sock);
    return NULL;
}

void *start_http_server(void *arg) {
    int http_server_fd, client_sock, c;
    struct sockaddr_in http_server, client;

    http_server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (http_server_fd == -1) {
        perror("Could not create HTTP socket");
        return NULL;
    }
    puts("HTTP Socket created");

    http_server.sin_family = AF_INET;
    http_server.sin_addr.s_addr = INADDR_ANY;
    http_server.sin_port = htons(HTTP_SERVER_PORT);

    if (bind(http_server_fd, (struct sockaddr *)&http_server, sizeof(http_server)) < 0) {
        perror("HTTP Bind failed");
        close(http_server_fd);
        return NULL;
    }
    puts("HTTP Bind done");

    listen(http_server_fd, 3);
    puts("HTTP server waiting for incoming connections...");
    c = sizeof(struct sockaddr_in);

    while ((client_sock = accept(http_server_fd, (struct sockaddr *)&client, (socklen_t *)&c))) {
        puts("HTTP Connection accepted");

        char buffer[BUFFER_SIZE];
        int read_size = recv(client_sock, buffer, sizeof(buffer) - 1, 0);
        if (read_size > 0) {
            buffer[read_size] = '\0';
            char *method = strtok(buffer, " ");
            char *file_requested = strtok(NULL, " ");
            if (method && file_requested && strcmp(method, "GET") == 0) {
                if (file_requested[0] == '/') file_requested++;
                char file_path[512];
                snprintf(file_path, sizeof(file_path), "%s%s", TMP_DIR, file_requested);
                FILE *file = fopen(file_path, "rb");
                if (file) {
                    fseek(file, 0, SEEK_END);
                    long file_size = ftell(file);
                    fseek(file, 0, SEEK_SET);

                    snprintf(buffer, sizeof(buffer), "HTTP/1.1 200 OK\r\nContent-Length: %ld\r\n\r\n", file_size);
                    send(client_sock, buffer, strlen(buffer), 0);

                    while ((read_size = fread(buffer, 1, BUFFER_SIZE, file)) > 0) {
                        send(client_sock, buffer, read_size, 0);
                    }
                    fclose(file);
                } else {
                    snprintf(buffer, sizeof(buffer), "HTTP/1.1 404 Not Found\r\n\r\n");
                    send(client_sock, buffer, strlen(buffer), 0);
                }
            }
        }
        close(client_sock);
    }

    close(http_server_fd);
    return NULL;
}
