#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>

// =============================
// Константы (чисто и явно)
// =============================
namespace cfg
{
constexpr int kStageCount = 3;
constexpr int kCarCount = 5;

constexpr int kFinishDistance = 100;
constexpr int kTrackWidth = 50;

constexpr char kFtokPath[] = "./ipc_keyfile_lab2";
constexpr int kProjMsg = 0x42;
constexpr int kProjShm = 0x43;

// Частота обновления экрана
constexpr useconds_t kRenderSleepUs = 200'000;

// Пауза ожиданий (старт/барьер)
constexpr useconds_t kWaitSleepUs = 1'000;
}  // namespace cfg

// =============================
// Утилиты
// =============================
static void DieSys(const char* what)
{
    std::cerr << what << ": " << std::strerror(errno) << "\n";
    std::exit(1);
}

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

// =============================
// SysV Messages: прогресс машин
// =============================
struct ProgressMessage
{
    long mtype{1};       // тип для msg queue
    int carId{};
    int distance{};      // 0..kFinishDistance
    int finished{};      // 0/1
};

// =============================
// Shared state: общий сигнал + барьер
// =============================
// ВАЖНО: это "разделяемый ресурс", доступный всем процессам.
// Тут и есть "общий сигнал в единственном экземпляре для всех участников". :contentReference[oaicite:5]{index=5}
struct SharedState
{
    // Текущий этап, который разрешено начинать (0..kStageCount)
    int startStage{0};

    // Текущий этап, который разрешено завершить и перейти дальше (0..kStageCount)
    int releaseStage{0};

    // Массив состояния: какой этап уже достиг барьера каждой машиной
    // (0 = ещё ничего, 1..kStageCount = дошёл до конца этапа N)
    int arrivedStage[cfg::kCarCount]{};
};

// =============================
// Машина-процесс
// =============================
class CarProcess
{
public:
    void Run(int carId, int progressQueueId, SharedState* shared)
    {
        std::mt19937 rng(static_cast<unsigned>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
            ^ (carId * 0x9e3779b9u)));

        std::uniform_int_distribution<int> stepDist(1, 10);
        std::uniform_int_distribution<int> sleepDist(100, 300);

        for (int stage = 1; stage <= cfg::kStageCount; ++stage)
        {
            // ---- Ожидаем общий старт-сигнал (в единственном экземпляре) ----
            // Арбитр ставит shared->startStage = stage один раз,
            // а все процессы просто читают эту одну общую переменную. 
            while (shared->startStage < stage)
                usleep(cfg::kWaitSleepUs);

            int distance = 0;

            // ---- Едем, периодически отправляя прогресс арбитру через сообщения ----
            while (distance < cfg::kFinishDistance)
            {
                distance = std::min(cfg::kFinishDistance, distance + stepDist(rng));

                ProgressMessage msg{};
                msg.carId = carId;
                msg.distance = distance;
                msg.finished = 0;

                if (msgsnd(progressQueueId, &msg, sizeof(msg) - sizeof(long), 0) == -1)
                    DieSys("msgsnd(progress)");

                usleep(static_cast<useconds_t>(sleepDist(rng) * 1000));
            }

            // ---- Сообщаем "финиш этапа" ----
            ProgressMessage done{};
            done.carId = carId;
            done.distance = cfg::kFinishDistance;
            done.finished = 1;

            if (msgsnd(progressQueueId, &done, sizeof(done) - sizeof(long), 0) == -1)
                DieSys("msgsnd(done)");

            // ---- Барьер: отмечаемся в массиве состояния ---- 
            shared->arrivedStage[carId] = stage;

            // ---- Ждём "разрешение продолжать" от арбитра (общий сигнал) ----
            // Арбитр выставляет shared->releaseStage = stage, когда все отметились.
            while (shared->releaseStage < stage)
                usleep(cfg::kWaitSleepUs);
        }
    }
};

// =============================
// Судья/арбитр (управляющий процесс)
// =============================
class RaceController
{
public:
    RaceController()
    {
        EnsureFtokFile();

        const key_t msgKey = ftok(cfg::kFtokPath, cfg::kProjMsg);
        if (msgKey == -1) DieSys("ftok(msgKey)");

        const key_t shmKey = ftok(cfg::kFtokPath, cfg::kProjShm);
        if (shmKey == -1) DieSys("ftok(shmKey)");

        progressQueueId_ = msgget(msgKey, IPC_CREAT | 0666);
        if (progressQueueId_ == -1) DieSys("msgget");

        shmId_ = shmget(shmKey, sizeof(SharedState), IPC_CREAT | 0666);
        if (shmId_ == -1) DieSys("shmget");

        shared_ = static_cast<SharedState*>(shmat(shmId_, nullptr, 0));
        if (shared_ == reinterpret_cast<void*>(-1)) DieSys("shmat");

        // Инициализация разделяемого состояния
        *shared_ = SharedState{};
    }

    ~RaceController()
    {
        // Дети уже должны завершиться, теперь чистим ресурсы
        if (shared_ && shared_ != reinterpret_cast<void*>(-1))
            shmdt(shared_);

        if (shmId_ != -1)
            shmctl(shmId_, IPC_RMID, nullptr);

        if (progressQueueId_ != -1)
            msgctl(progressQueueId_, IPC_RMID, nullptr);
    }

    void SpawnCars()
    {
        for (int carId = 0; carId < cfg::kCarCount; ++carId)
        {
            const pid_t pid = fork();
            if (pid == -1) DieSys("fork");

            if (pid == 0)
            {
                CarProcess car;
                car.Run(carId, progressQueueId_, shared_);
                std::exit(0);
            }

            carPids_[carId] = pid;
        }
    }

    void RunRace()
    {
        std::cin.clear();

        for (int stage = 1; stage <= cfg::kStageCount; ++stage)
        {
            RunStage(stage);

            ShowStageStandings(stage);

            if (stage == cfg::kStageCount)
            {
                WriteLine("\nГонка завершена");
            }
            else
            {
                WriteLine("\n----------------------------------------");
                WriteLine("Жми Enter для следующего этапа...");
                std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            }
        }

        ShowFinalStandings();

        for (pid_t pid : carPids_)
            waitpid(pid, nullptr, 0);
    }

private:
    struct CarView
    {
        int distance{0};
        int stagePlace{0};     // 1..kCarCount
        int totalScore{0};     // меньше = лучше
        bool finished{false};
    };

    void RunStage(int stage)
    {
        currentStage_ = stage;
        nextFinishPlace_ = 0;

        // Сброс состояния этапа (очки копятся)
        for (auto& car : cars_)
        {
            car.distance = 0;
            car.stagePlace = 0;
            car.finished = false;
        }

        // Сброс барьерного массива для данного этапа (формально необязателен,
        // но так чище и преподу приятнее)
        for (int i = 0; i < cfg::kCarCount; ++i)
            shared_->arrivedStage[i] = 0;

        // "Подготовка"
        ClearTerminal();
        WriteLine("Этап ", stage, "/", cfg::kStageCount, "\n");
        WriteLine("Подготовка этапа ", stage);
        sleep(1);

        // ---- Общий старт-сигнал в одном экземпляре ----
        // Одно присваивание, общий разделяемый ресурс, доступ всем сразу. 
        shared_->startStage = stage;

        // Основной цикл: читаем сообщения о прогрессе и рисуем
        while (!AllCarsFinished())
        {
            DrainProgressQueue();
            RenderFrame();
            usleep(cfg::kRenderSleepUs);
        }

        // Добираем хвост сообщений (чтобы не тащились в следующий этап)
        DrainProgressQueue();

        // ---- Барьер: ждём, пока все машины отметятся в массиве arrivedStage ---- 
        while (!AllCarsArrivedBarrier(stage))
            usleep(cfg::kWaitSleepUs);

        // ---- Разрешаем всем продолжать (один общий сигнал) ----
        shared_->releaseStage = stage;
    }

    void DrainProgressQueue()
    {
        ProgressMessage msg{};

        while (msgrcv(progressQueueId_, &msg, sizeof(msg) - sizeof(long), 0, IPC_NOWAIT) > 0)
        {
            if (msg.carId < 0 || msg.carId >= cfg::kCarCount)
                continue;

            auto& car = cars_[msg.carId];
            car.distance = msg.distance;

            if (msg.finished && !car.finished)
            {
                car.finished = true;
                car.stagePlace = ++nextFinishPlace_;
                car.totalScore += car.stagePlace; // меньше = лучше
            }
        }

        if (errno != ENOMSG && errno != 0)
            errno = 0; // не мешаем жить после ENOMSG
    }

    bool AllCarsFinished() const
    {
        for (const auto& car : cars_)
            if (!car.finished) return false;
        return true;
    }

    bool AllCarsArrivedBarrier(int stage) const
    {
        for (int i = 0; i < cfg::kCarCount; ++i)
            if (shared_->arrivedStage[i] < stage) return false;
        return true;
    }

    void RenderFrame() const
    {
        ClearTerminal();
        WriteLine("Этап ", currentStage_, "/", cfg::kStageCount, "\n");

        for (int i = 0; i < cfg::kCarCount; ++i)
        {
            const auto& car = cars_[i];

            int markerPos = (car.distance * cfg::kTrackWidth) / cfg::kFinishDistance;
            markerPos = std::clamp(markerPos, 0, cfg::kTrackWidth);

            std::string line;
            line.reserve(cfg::kTrackWidth + 1);
            line.append(markerPos, '-');
            line.push_back('>');
            line.append(cfg::kTrackWidth - markerPos, ' ');

            Write("car ", (i + 1), " |", line, "| ",
                  car.distance, " / ", cfg::kFinishDistance);

            if (car.finished)
                Write("  (place: ", car.stagePlace, ")");

            WriteLine();
        }
    }

    void ShowStageStandings(int stage) const
    {
        WriteLine("\nРезультаты этапа ", stage, ":");

        std::array<const CarView*, cfg::kCarCount> order{};
        for (int i = 0; i < cfg::kCarCount; ++i)
            order[i] = &cars_[i];

        auto IndexOf = [&](const CarView* c) -> int {
            return static_cast<int>(c - &cars_[0]);
        };

        // Стабильно: по месту этапа, при равенстве по номеру машины
        std::sort(order.begin(), order.end(),
                  [&](const CarView* a, const CarView* b)
                  {
                      if (a->stagePlace != b->stagePlace) return a->stagePlace < b->stagePlace;
                      return IndexOf(a) < IndexOf(b);
                  });

        for (int place = 0; place < cfg::kCarCount; ++place)
        {
            const CarView* car = order[place];
            const int carNo = IndexOf(car) + 1;
            WriteLine("Место ", place + 1, ": Машина ", carNo, " (Очки: ", car->totalScore, ")");
        }
    }

    void ShowFinalStandings() const
    {
        WriteLine("\n=== Итоги ===");

        std::array<const CarView*, cfg::kCarCount> ranking{};
        for (int i = 0; i < cfg::kCarCount; ++i)
            ranking[i] = &cars_[i];

        auto IndexOf = [&](const CarView* c) -> int {
            return static_cast<int>(c - &cars_[0]);
        };

        // Стабильно: меньше очков лучше, при равенстве по номеру машины
        std::sort(ranking.begin(), ranking.end(),
                  [&](const CarView* a, const CarView* b)
                  {
                      if (a->totalScore != b->totalScore) return a->totalScore < b->totalScore;
                      return IndexOf(a) < IndexOf(b);
                  });

        for (int place = 0; place < cfg::kCarCount; ++place)
        {
            const CarView* car = ranking[place];
            const int carNo = IndexOf(car) + 1;
            WriteLine("Место ", place + 1, ": Машина ", carNo, " (Всего очков: ", car->totalScore, ")");
        }
    }

    static void EnsureFtokFile()
    {
        // ftok требует существующий файл
        std::ifstream in(cfg::kFtokPath);
        if (in.good()) return;

        std::ofstream out(cfg::kFtokPath, std::ios::out | std::ios::trunc);
        if (!out) DieSys("create ftok file");
        out << "lab2\n";
    }

private:
    int progressQueueId_{-1};

    int shmId_{-1};
    SharedState* shared_{nullptr};

    int currentStage_{0};
    int nextFinishPlace_{0};

    std::array<pid_t, cfg::kCarCount> carPids_{};
    std::array<CarView, cfg::kCarCount> cars_{};
};

int main()
{
    RaceController race;
    race.SpawnCars();
    race.RunRace();
    return 0;
}