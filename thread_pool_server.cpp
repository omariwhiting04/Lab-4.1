#include <iostream>
#include <string>
#include <cstring>
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

static std::queue<int> task_queue;          // client_fds
static std::mutex queue_mtx;
static std::condition_variable cv;
static std::atomic<bool> stop_pool{false};
static const int NUM_THREADS = 10;

void worker_thread() {
    while (true) {
        int client_fd = -1;

        // wait for work or stop
        {
            std::unique_lock<std::mutex> lock(queue_mtx);
            cv.wait(lock, [] { return !task_queue.empty() || stop_pool.load(); });
            if (stop_pool && task_queue.empty()) return; // drain then exit
            client_fd = task_queue.front();
            task_queue.pop();
        }

        // handle client (echo once, like Part 1/2)
        char buffer[1024];
        ssize_t n = read(client_fd, buffer, sizeof(buffer) - 1);
        if (n > 0) {
            buffer[n] = '\0';
            std::cout << "Worker " << std::this_thread::get_id()
                      << " handled: \"" << buffer << "\"\n";
            write(client_fd, buffer, n);
        }
        close(client_fd);
    }
}

int main(int argc, char* argv[]) {
    int port = 8080;
    if (argc > 1) port = std::stoi(argv[1]);

    // socket setup (same as Part 1)
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

    std::cout << "Thread-pool echo server listening on port " << port
              << " with " << NUM_THREADS << " workers\n";

    // start worker threads
    std::vector<std::thread> workers;
    workers.reserve(NUM_THREADS);
    for (int i = 0; i < NUM_THREADS; ++i) workers.emplace_back(worker_thread);

    // accept loop -> enqueue client sockets
    while (true) {
        sockaddr_in client_addr{};
        socklen_t len = sizeof(client_addr);
        int client_fd = accept(server_fd, (sockaddr*)&client_addr, &len);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }

        {
            std::lock_guard<std::mutex> lock(queue_mtx);
            task_queue.push(client_fd);
        }
        cv.notify_one();
    }

    // graceful stop (if accept breaks)
    close(server_fd);
    {
        std::lock_guard<std::mutex> lock(queue_mtx);
        stop_pool = true;
    }
    cv.notify_all();
    for (auto& w : workers) w.join();
    return 0;
}
