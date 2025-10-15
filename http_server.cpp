#include <iostream>
#include <string>
#include <cstring>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

std::queue<int> task_queue;
std::mutex queue_mtx;
std::condition_variable cv;
std::atomic<bool> stop_pool{false};
const int NUM_THREADS = 10;

std::string parse_request(int client_fd) {
    char buffer[4096];
    ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
    if (bytes_read <= 0) return "";
    buffer[bytes_read] = '\0';
    std::string request(buffer);

    size_t pos = request.find("\r\n");
    std::string first_line = request.substr(0, pos);

    size_t method_end = first_line.find(' ');
    if (method_end == std::string::npos || first_line.substr(0, 3) != "GET")
        return "400";

    size_t path_end = first_line.find(' ', method_end + 1);
    std::string path = first_line.substr(method_end + 1, path_end - method_end - 1);
    if (path.empty() || path == "/") path = "/index.html";

    return path;
}

std::string get_file_content(const std::string &path) {
    if (path.find("..") != std::string::npos) return "";

    std::string full_path = "./www" + path;
    std::ifstream file(full_path, std::ios::binary);
    if (!file.is_open()) return "";

    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void send_response(int client_fd, const std::string &content, const std::string &content_type = "text/html") {
    std::string status = content.empty() ? "404 Not Found" : "200 OK";
    std::string response =
        "HTTP/1.1 " + status + "\r\n" +
        "Content-Type: " + content_type + "\r\n" +
        "Content-Length: " + std::to_string(content.size()) + "\r\n" +
        "Connection: close\r\n\r\n" +
        content;

    write(client_fd, response.c_str(), response.size());
}

void worker_thread() {
    while (true) {
        int client_fd = -1;
        {
            std::unique_lock<std::mutex> lock(queue_mtx);
            cv.wait(lock, [] { return !task_queue.empty() || stop_pool; });
            if (stop_pool && task_queue.empty()) return;
            client_fd = task_queue.front();
            task_queue.pop();
        }

        std::string path = parse_request(client_fd);
        if (path == "400") {
            send_response(client_fd, "<h1>400 Bad Request</h1>");
        } else {
            std::string content = get_file_content(path);
            send_response(client_fd, content);
        }
        close(client_fd);
    }
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) port = std::stoi(argv[1]);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(server_fd, 64) < 0) { perror("listen"); return 1; }

    std::cout << "HTTP server listening on port " << port << " (thread pool: " << NUM_THREADS << ")\n";

    std::vector<std::thread> workers;
    for (int i = 0; i < NUM_THREADS; ++i)
        workers.emplace_back(worker_thread);

    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &len);
        if (client_fd < 0) continue;
        {
            std::lock_guard<std::mutex> lock(queue_mtx);
            task_queue.push(client_fd);
        }
        cv.notify_one();
    }

    close(server_fd);
    {
        std::lock_guard<std::mutex> lock(queue_mtx);
        stop_pool = true;
    }
    cv.notify_all();
    for (auto &w : workers) w.join();
    return 0;
}
