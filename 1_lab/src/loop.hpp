#include "messages.hpp"
#include "queue.hpp"
#include "settings.hpp"
#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <ratio>
#include <sstream>
#include <string>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

class MainLoop
{
public:
    MainLoop(Settings settings) : settings_(settings), queue_(settings.number_message_queue) // очередь ограниченного размера
    {
        // Обновляем протоколы
        if (std::filesystem::exists(protocol_dir))
        {
            std::filesystem::remove_all(protocol_dir);
        }
        std::filesystem::create_directory(protocol_dir);

        // Открываем файл протокола работы очереди
        queue_protocol_.open(std::filesystem::path(protocol_dir) / "queue");

        // Настройка сигналов

        // Формируем набор сигналов для ожидания
        sigemptyset(&set_);
        sigaddset(&set_, SIGUSR1); // Сигнал - "появилась новая заявка"

        // Блокируем SIGUSR1 в текущем потоке
        pthread_sigmask(SIG_BLOCK, &set_, nullptr);

        // Флаг работы системы
        run_ = true;

        // Родительские процесс
        parent_pid_ = getpid();

        // Создание процессов станций
        for (const auto &station : settings_.stations_params)
        {
            const auto pid = fork();

            // Дочерний процесс (станция)
            if (pid == 0)
            {
                // Помещаем провецсс в группу родителя
                setpgid(0, parent_pid_);

                // В дочернем процессе запускае процесс обработки заявок
                thread_ = std::thread{&MainLoop::Consume, this, station};
                return;
            }

            // В родителе сохраняем идентификатор дочернего процесса
            processes_.push_back(pid);
        }

        // Поток генерации заявок (только в родителе)
        thread_ = std::thread{&MainLoop::Produce, this, settings_.create};
    }

    ~MainLoop()
    {
        // Останавливаем цикл работы
        run_ = false;

        // Ждем завершения всех дочерних процессов
        for (auto &i : processes_)
        {
            int status;
            waitpid(i, &status, 0);
        }

        // Ждем завершения рабочего потока
        thread_.join();
    }

private:
    // Генерация заявок
    void Produce(Params par)
    {
        // Протокол отклоненных заявок (текстовый файл)
        std::ofstream protocol{std::filesystem::path(protocol_dir) / "rejected"};

        // Генератор случайных чисел
        std::random_device rd;
        std::mt19937 gen(rd());

        // Счетчик заявок
        int id = 0;

        while (run_)
        {
            // Случайный тип газа
            const auto type = type_gas_(gen);

            // Случайная задержка между заявками
            const auto time = GetRandomTime(par);

            // Имитируем работу до следующей заявки
            std::this_thread::sleep_for(std::chrono::milliseconds(time));

            // Формируем сообщение
            Message mes;
            mes.gas_type = static_cast<GasType>(type);
            mes.id = id++;

            // Пытаемся добавить заявку в очередь
            const auto added_mes = queue_.Push(mes);

            // Если очередеь переполнена заявка отклонена
            if (mes.status == REJECTED)
            {
                protocol << added_mes << std::endl;
            }
            std::cout << "Новая заявка: " << mes << std::endl;
            queue_protocol_ << "Новая заявка: " << mes << std::endl;

            // Уведомляем станции через сигнал
            killpg(parent_pid_, SIGUSR1);
        }
    }

    // Обработчик заявок станций
    void Consume(Station station)
    {
        // Имя файла протокола станции
        std::stringstream name_protocol{};
        name_protocol << ToString(station.type) << " " << getpid();

        std::ofstream protocol{std::filesystem::path(protocol_dir) / name_protocol.str()};

        while (run_)
        {
            int sig;
            sigwait(&set_, &sig);

            while (auto mes = queue_.Pop(station.type))
            {
                std::cout << "ID Station: " << station.id << " --> " << mes.value() << std::endl;
                queue_protocol_ << "ID Station: " << station.id << " --> " << mes.value() << std::endl;

                const auto time = GetRandomTime(station.handle);
                std::this_thread::sleep_for(std::chrono::milliseconds(time));
                mes->status = PROCESSED;

                std::cout << "ID Station: " << station.id << " --> " << mes.value() << std::endl;
                protocol << "ID Station: " << station.id << " --> " << mes.value() << std::endl;
                queue_protocol_ << "ID Station: " << station.id << " --> " << mes.value() << std::endl;
            }
        }
    }

    int GetRandomTime(Params st)
    {
        double mean = st.mean;
        double stddev = st.stddev;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<double> dist(mean, stddev);

        return dist(gen);
    }

private:
    Queue queue_;
    std::atomic<bool> run_{false};

    sigset_t set_;
    pid_t parent_pid_;

    std::thread thread_;
    std::vector<pid_t> processes_{};

    std::ofstream queue_protocol_;

private:
    Settings settings_;
    std::uniform_int_distribution<int> type_gas_{0, 2};
};