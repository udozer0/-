#include <condition_variable>
#include <iostream>             // ← обязательно!
#include "loop.hpp"

std::condition_variable notify;
std::mutex cond_lock;

void signalHandler(int signum) {
    std::cout << "Завершаем..." << std::endl;
    notify.notify_all();
}

int main(int argc, char **argv) {
    std::signal(SIGINT, signalHandler);

    Settings settings;

    if (argc > 1) {   // было > 0 — это всегда true
        std::string path_to_settings = argv[1];

        if (std::filesystem::exists(path_to_settings)) {
            auto set = GetSettings(path_to_settings);
            if (set.has_value()) {
                settings = set.value();
                std::cout << "Прочитана конфигурация " << path_to_settings << std::endl;
            }
        }
    }

    MainLoop loop(settings);

    std::unique_lock<std::mutex> lock(cond_lock);
    notify.wait(lock);

    return 0;
}