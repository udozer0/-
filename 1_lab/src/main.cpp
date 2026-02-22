#include <condition_variable>
#include <iostream>
#include "loop.hpp"

std::condition_variable notify;
std::mutex cond_lock;

void signalHandler(int) {
    std::cout << "Завершаем..." << std::endl;
    notify.notify_all();
}

int main(int argc, char** argv) {
    std::signal(SIGINT, signalHandler);

    Settings settings;

    if (argc > 1) {
        std::string path = argv[1];
        if (std::filesystem::exists(path)) {
            auto set = GetSettings(path);
            if (set.has_value()) {
                settings = set.value();
                std::cout << "Прочитана конфигурация " << path << std::endl;
            }
        }
    }

    MainLoop loop(settings);

    std::unique_lock<std::mutex> lock(cond_lock);
    notify.wait(lock);
    return 0;
}