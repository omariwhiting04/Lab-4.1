#include <iostream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <cstdlib>

void* handle_client(void* arg) {
    int client_fd = *(int*)arg;
    free(arg);

    char buffer[1024];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read > 0) {
        buffer[bytes_read] = '\0';
        std::cout << "Received: " << buffer << std::endl;
        write(client_fd, buffer, bytes_read); // Echo back
    }

    close(client_fd);
    return nullptr;
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) port = std::stoi(argv[1]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(server_fd, 10) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "Multi-threaded echo server listening on port " << port << std::endl;

    while (true) {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) { perror("accept"); continue; }

        std::cout << "Client connected from " 
                  << inet_ntoa(client_addr.sin_addr) << std::endl;

        int* client_ptr = (int*)malloc(sizeof(int));
        *client_ptr = client_fd;

        pthread_t thread;
        if (pthread_create(&thread, nullptr, handle_client, client_ptr) != 0) {
            perror("pthread_create");
            close(client_fd);
            free(client_ptr);
            continue;
        }

        pthread_detach(thread); // Don't need to join
    }

    close(server_fd);
    return 0;
}
