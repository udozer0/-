#include <sys/msg.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <string>

namespace
{
constexpr int number_parts = 3;    // этапы
constexpr int cars_number = 5;
constexpr int finish_line = 100;
constexpr int track_len = 50;      // длина полоски

std::atomic_bool start_flag {};
std::atomic_bool next_flag {};
}  // namespace

template <typename... Args>
static void Print(Args&&... args)
{
    (std::cout << ... << std::forward<Args>(args));
    std::cout.flush();
}

template <typename... Args>
static void Println(Args&&... args)
{
    (std::cout << ... << std::forward<Args>(args));
    std::cout << '\n';
    std::cout.flush();
}

struct ProgressMessage
{
    long mtype { 1 };
    int id {};
    int progress {};
    int finished {};
};

class Car
{
public:
    void start(int id, int queue)
    {
        signal(SIGUSR1, [](int) { start_flag = true; });
        signal(SIGUSR2, [](int) { next_flag = true; });

        std::mt19937 gen(std::random_device {}());
        std::uniform_int_distribution<> step_dist(1, 10);
        std::uniform_int_distribution<> sleep_dist(100, 300);

        for (int stage = 1; stage <= number_parts; ++stage)
        {
            while (!start_flag) pause();
            start_flag = false;

            progress = 0;
            finished = false;
            order = 0;

            while (progress < finish_line)
            {
                progress = std::min(progress + step_dist(gen), finish_line);

                ProgressMessage msg {};
                msg.id = id;
                msg.progress = progress;

                msgsnd(queue, &msg, sizeof(msg) - sizeof(long), 0);

                usleep(sleep_dist(gen) * 1000);
            }

            ProgressMessage done {};
            done.id = id;
            done.progress = progress;
            done.finished = 1;

            msgsnd(queue, &done, sizeof(done) - sizeof(long), 0);

            if (stage == number_parts) continue;

            while (!next_flag) pause();
            next_flag = false;
        }
    }

    int progress {};
    int order {};      // место на этапе (1..cars_number)
    int points {};     // суммарные очки (меньше = лучше)
    bool finished {};
};

class Arbiter
{
public:
    ~Arbiter()
    {
        msgctl(progress_queue, IPC_RMID, nullptr);
    }

    void prepare()
    {
        for (int i = 0; i < cars_number; ++i)
        {
            pid_t pid = fork();

            if (pid == 0)
            {
                if (i == 0) process_group = getpid();
                setpgid(0, process_group);

                cars[i].start(i, progress_queue);
                std::exit(0);
            }

            processes[i] = pid;

            if (i == 0) process_group = pid;
            setpgid(pid, process_group);
        }
    }

    void start()
    {
        std::cin.clear();

        for (int stage = 1; stage <= number_parts; ++stage)
        {
            current_stage = stage;
            finish_order_counter = 0;

            for (auto& car : cars)
            {
                car.progress = 0;
                car.order = 0;
                car.finished = false;
                // points не трогаем (копим)
            }

            ClearScreen();
            Println("Этап ", stage, "/", number_parts, "\n");
            Println("Подготовка этапа ", stage);
            sleep(1);

            // стартуем машины
            kill(-process_group, SIGUSR1);

            unsigned finished_count = 0;

            while (finished_count < cars_number)
            {
                ProgressMessage msg {};

                // читаем все доступные сообщения
                while (msgrcv(progress_queue, &msg,
                              sizeof(msg) - sizeof(long),
                              0, IPC_NOWAIT) > 0)
                {
                    Car& car = cars[msg.id];
                    car.progress = msg.progress;

                    if (msg.finished && !car.finished)
                    {
                        car.finished = true;
                        car.order = ++finish_order_counter;
                        car.points += car.order; // меньше очков = лучше
                    }
                }

                finished_count = 0;
                for (const auto& car : cars)
                    if (car.finished) ++finished_count;

                DisplayProgressMpiStyle();
                usleep(200000);
            }

            DisplayPoints();

            if (stage == number_parts)
            {
                Println("\nГонка завершена");
                continue;
            }

            Println("\n----------------------------------------");
            Println("Жми Enter для следующего этапа...");
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

            // следующий этап
            kill(-process_group, SIGUSR2);
        }

        DisplayResults();

        for (const auto& pid : processes)
            waitpid(pid, nullptr, 0);
    }

private:
    static void ClearScreen()
    {
        Print("\033[2J\033[H");
    }

    // интерфейс как в MPI + число/100
    void DisplayProgressMpiStyle() const
    {
        ClearScreen();
        Println("Этап ", current_stage, "/", number_parts, "\n");

        for (int r = 0; r < cars_number; ++r)
        {
            const Car& car = cars[r];

            int p = (car.progress * track_len) / finish_line;
            p = std::clamp(p, 0, track_len);

            std::string line;
            line.reserve(track_len + 1);
            line.append(p, '-');
            line.push_back('>');
            line.append(track_len - p, ' ');

            Print("car ", (r + 1), " |", line, "| ",
                  car.progress, " / ", finish_line);

            if (car.finished)
                Print("  (place: ", car.order, ")");

            Println();
        }
    }

    void DisplayPoints() const
    {
        Println("\nРезультаты этапа:");

        std::array<const Car*, cars_number> order_ptrs {};
        for (int i = 0; i < cars_number; ++i)
            order_ptrs[i] = &cars[i];

        auto idx = [&](const Car* c) -> int {
            return static_cast<int>(c - &cars[0]); // 0..cars_number-1
        };

        // Стабильно: сначала по order, если равны (на всякий) по номеру машины
        std::sort(order_ptrs.begin(), order_ptrs.end(),
                  [&](const Car* a, const Car* b)
                  {
                      if (a->order != b->order) return a->order < b->order;
                      return idx(a) < idx(b);
                  });

        for (int place = 0; place < cars_number; ++place)
        {
            const Car* car = order_ptrs[place];
            int car_no = idx(car) + 1;

            Println("Место ", place + 1,
                    ": Машина ", car_no,
                    " (Очки: ", car->points, ")");
        }
    }

    void DisplayResults() const
    {
        Println("\n=== Итоги ===");

        std::array<const Car*, cars_number> score_ptrs {};
        for (int i = 0; i < cars_number; ++i)
            score_ptrs[i] = &cars[i];

        auto idx = [&](const Car* c) -> int {
            return static_cast<int>(c - &cars[0]); // 0..cars_number-1
        };

        // Стабильно: меньше очков лучше, при равенстве меньше номер машины лучше
        std::sort(score_ptrs.begin(), score_ptrs.end(),
                  [&](const Car* a, const Car* b)
                  {
                      if (a->points != b->points) return a->points < b->points;
                      return idx(a) < idx(b);
                  });

        for (int place = 0; place < cars_number; ++place)
        {
            const Car* car = score_ptrs[place];
            int car_no = idx(car) + 1;

            Println("Место ", place + 1,
                    ": Машина ", car_no,
                    " (Всего очков: ", car->points, ")");
        }
    }

private:
    std::array<pid_t, cars_number> processes {};
    pid_t process_group {};

    int progress_queue { msgget(IPC_PRIVATE, IPC_CREAT | 0666) };

    int current_stage {};
    int finish_order_counter {};
    std::array<Car, cars_number> cars {};
};

int main()
{
    Arbiter arbiter;
    arbiter.prepare();
    arbiter.start();
    return 0;
}