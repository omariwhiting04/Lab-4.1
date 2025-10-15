#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char* argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <host> <port> <message>\n";
        return 1;
    }
    std::string host = argv[1];
    int port = std::stoi(argv[2]);
    std::string message = argv[3];

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd < 0) { perror("socket"); return 1; }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);

    if (connect(client_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect"); return 1;
    }

    write(client_fd, message.c_str(), message.length());
    char buffer[1024];
    ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
    if (n > 0) {
        buffer[n] = '\0';
        std::cout << "Echo: " << buffer << std::endl;
    }
    close(client_fd);
    return 0;
}
