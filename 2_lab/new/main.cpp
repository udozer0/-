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
constexpr int kStageCount = 3;
constexpr int kCarCount = 5;

constexpr int kFinishDistance = 100;
constexpr int kTrackWidth = 50;

// Сигналы для синхронизации этапов
std::atomic_bool gStartStageFlag {};
std::atomic_bool gNextStageFlag {};
}  // namespace

template <typename... Args>
static void Write(Args&&... args)
{
    (std::cout << ... << std::forward<Args>(args));
    std::cout.flush();
}

template <typename... Args>
static void WriteLine(Args&&... args)
{
    (std::cout << ... << std::forward<Args>(args));
    std::cout << '\n';
    std::cout.flush();
}

static void ClearTerminal()
{
    Write("\033[2J\033[H");
}

struct ProgressMessage
{
    long mtype { 1 };   // для System V msg (тип сообщения)
    int carId {};
    int distance {};
    int finished {};
};

struct CarState
{
    int distance { 0 };
    int stagePlace { 0 };     // 1..kCarCount
    int totalScore { 0 };     // меньше = лучше
    bool finishedStage { false };
};

class CarProcess
{
public:
    void Run(int carId, int progressQueueId)
    {
        // Получаем сигналы от судьи
        signal(SIGUSR1, [](int) { gStartStageFlag = true; });
        signal(SIGUSR2, [](int) { gNextStageFlag = true; });

        std::mt19937 rng(std::random_device {}());
        std::uniform_int_distribution<int> stepDist(1, 10);
        std::uniform_int_distribution<int> sleepDist(100, 300);

        for (int stage = 1; stage <= kStageCount; ++stage)
        {
            // Ждём старт этапа
            while (!gStartStageFlag) pause();
            gStartStageFlag = false;

            int distance = 0;

            // Едем до финиша, отправляя прогресс судье
            while (distance < kFinishDistance)
            {
                distance = std::min(distance + stepDist(rng), kFinishDistance);

                ProgressMessage msg {};
                msg.carId = carId;
                msg.distance = distance;
                msg.finished = 0;

                msgsnd(progressQueueId, &msg, sizeof(msg) - sizeof(long), 0);
                usleep(sleepDist(rng) * 1000);
            }

            // Сообщаем "я финишировал"
            ProgressMessage done {};
            done.carId = carId;
            done.distance = distance;
            done.finished = 1;

            msgsnd(progressQueueId, &done, sizeof(done) - sizeof(long), 0);

            if (stage == kStageCount) continue;

            // Ждём разрешения на следующий этап
            while (!gNextStageFlag) pause();
            gNextStageFlag = false;
        }
    }
};

class RaceController
{
public:
    RaceController()
        : progressQueueId_(msgget(IPC_PRIVATE, IPC_CREAT | 0666))
    {
    }

    ~RaceController()
    {
        msgctl(progressQueueId_, IPC_RMID, nullptr);
    }

    void SpawnCars()
    {
        for (int i = 0; i < kCarCount; ++i)
        {
            pid_t pid = fork();

            if (pid == 0)
            {
                // Ребёнок: в одну группу процессов, чтобы kill(-pgid, SIGxxx) работал
                if (i == 0) processGroupId_ = getpid();
                setpgid(0, processGroupId_);

                CarProcess car;
                car.Run(i, progressQueueId_);
                std::exit(0);
            }

            // Родитель
            carPids_[i] = pid;

            if (i == 0) processGroupId_ = pid;
            setpgid(pid, processGroupId_);
        }
    }

    void RunRace()
    {
        std::cin.clear();

        for (int stage = 1; stage <= kStageCount; ++stage)
        {
            RunStage(stage);

            if (stage == kStageCount)
            {
                WriteLine("\nГонка завершена");
            }
            else
            {
                WriteLine("\n----------------------------------------");
                WriteLine("Жми Enter для следующего этапа...");
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

                // Разрешаем всем машинам переходить к следующему этапу
                kill(-processGroupId_, SIGUSR2);
            }
        }

        ShowFinalStandings();

        for (pid_t pid : carPids_)
            waitpid(pid, nullptr, 0);
    }

private:
    void RunStage(int stageNumber)
    {
        currentStage_ = stageNumber;
        nextFinishPlace_ = 0;

        // Сброс состояния этапа (очки копим)
        for (auto& car : cars_)
        {
            car.distance = 0;
            car.stagePlace = 0;
            car.finishedStage = false;
        }

        ClearTerminal();
        WriteLine("Этап ", stageNumber, "/", kStageCount, "\n");
        WriteLine("Подготовка этапа ", stageNumber);
        sleep(1);

        // Стартуем этап
        kill(-processGroupId_, SIGUSR1);

        while (!AllCarsFinishedStage())
        {
            DrainProgressQueue();
            RenderStageFrame();
            usleep(200000);
        }

        // На всякий: дочитать остатки, если прилетели после последнего цикла
        DrainProgressQueue();

        ShowStageStandings();
    }

    bool AllCarsFinishedStage() const
    {
        for (const auto& car : cars_)
            if (!car.finishedStage) return false;
        return true;
    }

    void DrainProgressQueue()
    {
        ProgressMessage msg {};

        while (msgrcv(progressQueueId_, &msg,
                      sizeof(msg) - sizeof(long),
                      0, IPC_NOWAIT) > 0)
        {
            auto& car = cars_.at(msg.carId);
            car.distance = msg.distance;

            if (msg.finished && !car.finishedStage)
            {
                car.finishedStage = true;
                car.stagePlace = ++nextFinishPlace_;

                // Меньше очков = лучше. 1 место добавляет 1, 5 место добавляет 5.
                car.totalScore += car.stagePlace;
            }
        }
    }

    void RenderStageFrame() const
    {
        ClearTerminal();
        WriteLine("Этап ", currentStage_, "/", kStageCount, "\n");

        for (int i = 0; i < kCarCount; ++i)
        {
            const auto& car = cars_[i];

            int markerPos = (car.distance * kTrackWidth) / kFinishDistance;
            markerPos = std::clamp(markerPos, 0, kTrackWidth);

            std::string line;
            line.reserve(kTrackWidth + 1);
            line.append(markerPos, '-');
            line.push_back('>');
            line.append(kTrackWidth - markerPos, ' ');

            Write("car ", (i + 1), " |", line, "| ",
                  car.distance, " / ", kFinishDistance);

            if (car.finishedStage)
                Write("  (place: ", car.stagePlace, ")");

            WriteLine();
        }
    }

    void ShowStageStandings() const
    {
        WriteLine("\nРезультаты этапа:");

        std::array<const CarState*, kCarCount> order {};
        for (int i = 0; i < kCarCount; ++i)
            order[i] = &cars_[i];

        auto CarIndex = [&](const CarState* c) -> int {
            return static_cast<int>(c - &cars_[0]);
        };

        std::sort(order.begin(), order.end(),
                  [&](const CarState* a, const CarState* b)
                  {
                      if (a->stagePlace != b->stagePlace) return a->stagePlace < b->stagePlace;
                      return CarIndex(a) < CarIndex(b);
                  });

        for (int place = 0; place < kCarCount; ++place)
        {
            const CarState* car = order[place];
            int carNumber = CarIndex(car) + 1;

            WriteLine("Место ", place + 1,
                      ": Машина ", carNumber,
                      " (Очки: ", car->totalScore, ")");
        }
    }

    void ShowFinalStandings() const
    {
        WriteLine("\n=== Итоги ===");

        std::array<const CarState*, kCarCount> ranking {};
        for (int i = 0; i < kCarCount; ++i)
            ranking[i] = &cars_[i];

        auto CarIndex = [&](const CarState* c) -> int {
            return static_cast<int>(c - &cars_[0]);
        };

        std::sort(ranking.begin(), ranking.end(),
                  [&](const CarState* a, const CarState* b)
                  {
                      if (a->totalScore != b->totalScore) return a->totalScore < b->totalScore;
                      return CarIndex(a) < CarIndex(b);
                  });

        for (int place = 0; place < kCarCount; ++place)
        {
            const CarState* car = ranking[place];
            int carNumber = CarIndex(car) + 1;

            WriteLine("Место ", place + 1,
                      ": Машина ", carNumber,
                      " (Всего очков: ", car->totalScore, ")");
        }
    }

private:
    std::array<pid_t, kCarCount> carPids_ {};
    pid_t processGroupId_ {};

    int progressQueueId_ {};

    int currentStage_ {};
    int nextFinishPlace_ {};

    std::array<CarState, kCarCount> cars_ {};
};

int main()
{
    RaceController race;
    race.SpawnCars();
    race.RunRace();
    return 0;
}