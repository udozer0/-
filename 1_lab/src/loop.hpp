#include "messages.hpp"
#include "queue.hpp"
#include "settings.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

class MainLoop {
public:
    MainLoop(Settings settings) : settings_(settings), queue_(settings.number_message_queue) {
        if (std::filesystem::exists(protocol_dir)) std::filesystem::remove_all(protocol_dir);
        std::filesystem::create_directory(protocol_dir);

        queue_protocol_.open(std::filesystem::path(protocol_dir) / "queue.log");

        parent_pid_ = getpid();
        run_ = true;

        // Создаём 5 процессов-станций
        for (const auto& station : settings_.stations_params) {
            pid_t pid = fork();
            if (pid == 0) {
                setpgid(0, parent_pid_);
                Consume(station);               // дочерний процесс не возвращается
                return;
            }
            processes_.push_back(pid);
        }

        // Генератор в родительском процессе
        thread_ = std::thread{&MainLoop::Produce, this, settings_.create};
    }

    ~MainLoop() {
        run_ = false;
        for (auto pid : processes_) waitpid(pid, nullptr, 0);
        if (thread_.joinable()) thread_.join();
    }

private:
    void Produce(Params par) {
        std::ofstream rejected(std::filesystem::path(protocol_dir) / "rejected.log");
        std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<> type_dist(0, 2);
        int id = 0;

        while (run_) {
            GasType type = static_cast<GasType>(type_dist(gen));
            auto sleep_ms = GetRandomTime(par);

            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));

            Message mes{id++, type, EXPECTED};

            if (!queue_.Push(mes)) {
                mes.status = REJECTED;
                rejected << mes << std::endl;
            }

            queue_protocol_ << "Новая заявка: " << mes << std::endl;
            std::cout << "Новая заявка: " << mes << std::endl;
        }
    }

    void Consume(Station station) {
        std::stringstream ss;
        ss << ToString(station.type) << "_" << getpid() << ".log";
        std::ofstream protocol(std::filesystem::path(protocol_dir) / ss.str());

        while (run_) {
            auto opt = queue_.Pop(station.type);
            if (!opt) continue;

            Message mes = opt.value();
            std::cout << "Станция " << station.id << " обслуживает " << mes << std::endl;
            queue_protocol_ << "Станция " << station.id << " --> " << mes << std::endl;

            auto sleep_ms = GetRandomTime(station.handle);
            std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));

            mes.status = PROCESSED;
            protocol << mes << std::endl;
            queue_protocol_ << "Обслужено: " << mes << std::endl;
        }
        exit(0);
    }

    long GetRandomTime(Params p) {
        static thread_local std::mt19937 gen{std::random_device{}()};
        std::normal_distribution<double> dist(p.mean, p.stddev);
        return std::max(1L, static_cast<long>(dist(gen)));
    }

private:
    Queue queue_;
    std::atomic<bool> run_{true};
    pid_t parent_pid_;
    std::thread thread_;
    std::vector<pid_t> processes_;
    std::ofstream queue_protocol_;
    Settings settings_;
};