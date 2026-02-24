#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string>
#include <vector>

// =============================
// Константы
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

// Файлы блокировки этапов (3 штуки)
constexpr char kStageLock1[] = "./stage1.lock";
constexpr char kStageLock2[] = "./stage2.lock";
constexpr char kStageLock3[] = "./stage3.lock";

constexpr useconds_t kRenderSleepUs = 200'000;
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

static void EnsureFileExists(const char* path)
{
    std::ifstream in(path);
    if (in.good()) return;

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) DieSys("create file");
    out << "lock\n";
}

static int OpenStageLockFile(int stage)
{
    const char* path = nullptr;
    switch (stage)
    {
        case 1: path = cfg::kStageLock1; break;
        case 2: path = cfg::kStageLock2; break;
        case 3: path = cfg::kStageLock3; break;
        default: DieSys("bad stage"); break;
    }

    int fd = ::open(path, O_RDWR | O_CREAT, 0666);
    if (fd == -1) DieSys("open(stage lock file)");
    return fd;
}

// =============================
// SysV Messages: прогресс машин
// =============================
struct ProgressMessage
{
    long mtype{1};
    int carId{};
    int distance{};
    int finished{};
};

// =============================
// Shared state: барьер
// =============================
struct SharedState
{
    // На каком этапе уже разрешили продолжить (0..kStageCount)
    int releaseStage{0};

    // Массив состояния: до какого этапа дошла каждая машина (0..kStageCount)
    int arrivedStage[cfg::kCarCount]{};
};

// =============================
// Машина-процесс
// =============================
class CarProcess
{
public:
    // inheritedStageLockFds: fd, которые родитель держал залоченными при fork (их надо закрыть в child)
    void Run(int carId, int progressQueueId, SharedState* shared,
             const std::vector<int>& inheritedStageLockFds)
    {
        // Критично: закрываем унаследованные fd лок-файлов,
        // иначе ребёнок "унаследует" эксклюзивный lock и не будет блокироваться.
        for (int fd : inheritedStageLockFds)
        {
            if (fd >= 0) ::close(fd);
        }

        std::mt19937 rng(static_cast<unsigned>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
            ^ (carId * 0x9e3779b9u)));

        std::uniform_int_distribution<int> stepDist(1, 10);
        std::uniform_int_distribution<int> sleepDist(100, 300);

        for (int stage = 1; stage <= cfg::kStageCount; ++stage)
        {
            // ---- Ждём старт этапа через файл-блокировку ----
            // Арбитр держит LOCK_EX. Мы пытаемся взять LOCK_SH и блокируемся,
            // пока арбитр не снимет эксклюзивную блокировку.
            WaitStageStart(stage);

            int distance = 0;

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

            ProgressMessage done{};
            done.carId = carId;
            done.distance = cfg::kFinishDistance;
            done.finished = 1;

            if (msgsnd(progressQueueId, &done, sizeof(done) - sizeof(long), 0) == -1)
                DieSys("msgsnd(done)");

            // ---- Барьер: отмечаемся ----
            shared->arrivedStage[carId] = stage;

            // ---- Ждём, пока арбитр разрешит переход дальше ----
            while (shared->releaseStage < stage)
                usleep(cfg::kWaitSleepUs);
        }
    }

private:
    static void WaitStageStart(int stage)
    {
        int fd = OpenStageLockFile(stage);

        // Берём shared lock (блокируется, если арбитр держит exclusive)
        if (flock(fd, LOCK_SH) == -1)
            DieSys("flock(LOCK_SH) in car");

        // Сразу отпускаем. Нам нужно только "проскочить" после старта.
        if (flock(fd, LOCK_UN) == -1)
            DieSys("flock(LOCK_UN) in car");

        ::close(fd);
    }
};

// =============================
// Судья/арбитр
// =============================
class RaceController
{
public:
    RaceController()
    {
        // ftok файл
        EnsureFileExists(cfg::kFtokPath);

        // stage lock файлы (3 штуки)
        EnsureFileExists(cfg::kStageLock1);
        EnsureFileExists(cfg::kStageLock2);
        EnsureFileExists(cfg::kStageLock3);

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

        *shared_ = SharedState{};

        // До старта: блокируем ВСЕ этапы эксклюзивно.
        // Машины будут ждать shared lock и стартуют только когда мы unlock.
        LockAllStagesExclusive();
    }

    ~RaceController()
    {
        // снять блокировки (на всякий)
        UnlockAllStages();

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
                car.Run(carId, progressQueueId_, shared_, stageLockFds_);
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
        int stagePlace{0};
        int totalScore{0};   // меньше = лучше
        bool finished{false};
    };

    void LockAllStagesExclusive()
    {
        stageLockFds_.clear();
        stageLockFds_.reserve(cfg::kStageCount);

        for (int stage = 1; stage <= cfg::kStageCount; ++stage)
        {
            int fd = OpenStageLockFile(stage);
            if (flock(fd, LOCK_EX) == -1) DieSys("flock(LOCK_EX) in arbiter");
            stageLockFds_.push_back(fd);
        }
    }

    void UnlockStage(int stage)
    {
        // stage: 1..3 -> index 0..2
        int idx = stage - 1;
        if (idx < 0 || idx >= static_cast<int>(stageLockFds_.size()))
            DieSys("UnlockStage: bad idx");

        int fd = stageLockFds_[idx];
        if (fd < 0) return;

        if (flock(fd, LOCK_UN) == -1) DieSys("flock(LOCK_UN) in arbiter");
        // Можно закрыть, этап больше не понадобится
        ::close(fd);
        stageLockFds_[idx] = -1;
    }

    void UnlockAllStages()
    {
        for (int& fd : stageLockFds_)
        {
            if (fd >= 0)
            {
                flock(fd, LOCK_UN);
                ::close(fd);
                fd = -1;
            }
        }
    }

    void RunStage(int stage)
    {
        currentStage_ = stage;
        nextFinishPlace_ = 0;

        for (auto& car : cars_)
        {
            car.distance = 0;
            car.stagePlace = 0;
            car.finished = false;
        }

        // сброс барьера для этапа
        for (int i = 0; i < cfg::kCarCount; ++i)
            shared_->arrivedStage[i] = 0;

        ClearTerminal();
        WriteLine("Этап ", stage, "/", cfg::kStageCount, "\n");
        WriteLine("Подготовка этапа ", stage);
        sleep(1);

        // ---- Старт этапа: снимаем эксклюзивную блокировку с файла этапа ----
        UnlockStage(stage);

        while (!AllCarsFinished())
        {
            DrainProgressQueue();
            RenderFrame();
            usleep(cfg::kRenderSleepUs);
        }

        DrainProgressQueue();

        // ---- Барьер: ждём arrivedStage ----
        while (!AllCarsArrivedBarrier(stage))
            usleep(cfg::kWaitSleepUs);

        // ---- Разрешаем всем продолжать ----
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
                car.totalScore += car.stagePlace;
            }
        }

        if (errno == ENOMSG) errno = 0;
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
            WriteLine("Место ", place + 1, ": Машина ", carNo,
                      " (Всего очков: ", car->totalScore, ")");
        }
    }

private:
    int progressQueueId_{-1};

    int shmId_{-1};
    SharedState* shared_{nullptr};

    int currentStage_{0};
    int nextFinishPlace_{0};

    std::array<pid_t, cfg::kCarCount> carPids_{};
    std::array<CarView, cfg::kCarCount> cars_{};

    // fd файлов блокировки этапов (родитель держит LOCK_EX до старта)
    std::vector<int> stageLockFds_;
};

int main()
{
    RaceController race;
    race.SpawnCars();
    race.RunRace();
    return 0;
}