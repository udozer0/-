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

// Константы
namespace cfg
{
// этапы
constexpr int kStageCount = 3;
// кол-во машин 
constexpr int kCarCount = 5;

constexpr int kFinishDistance = 100;
constexpr int kTrackWidth = 50;

constexpr char kFtokPath[] = "./ipc_keyfile_lab2";
constexpr int kProjMsg = 1;
constexpr int kProjShm = 1;

// Файлы блокировки этапов (3 штуки)
constexpr char kStageLock1[] = "./stage1.lock";
constexpr char kStageLock2[] = "./stage2.lock";
constexpr char kStageLock3[] = "./stage3.lock";

constexpr useconds_t kRenderSleepUs = 200'000;
constexpr useconds_t kWaitSleepUs = 1'000;
}  

// Утилиты
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

// Создает файл, если его нет
static void EnsureFileExists(const char* path)
{
    std::ifstream in(path);
    if (in.good()) return;

    std::ofstream out(path, std::ios::out | std::ios::trunc);
    if (!out) DieSys("create file");
    out << "lock\n";
}

// Выбирает правильный stageN.lock
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

    int fileDescriptor = ::open(path, O_RDWR | O_CREAT, 0666);
    if (fileDescriptor == -1) DieSys("open(stage lock file)");
    return fileDescriptor;
}

// SysV Messages: прогресс машин
struct ProgressMessage
{
    // Тип сообщения для SysV очереди. один поток сообщений, поэтому фиксируем 1
    long mtype{1};

    int carId{};
    //Текущая пройденная дистанция на этап
    int distance{};
    //Флаг завершения этапа
    int finished{};
};

/// Shared memory для барьерной синхронизации между машинами и арбитром.
/// Используется для отметки завершения этапа и разрешения перехода дальше.
struct SharedState
{
    /// Номер этапа, для которого арбитр разрешил продолжение.
    /// Машины ждут, пока releaseStage >= текущий этап.
    int releaseStage{0};

    /// arrivedStage[i] = N означает, что машина i
    /// завершила этап N и вошла в барьер.
    int arrivedStage[cfg::kCarCount]{};
};

// Машина-процесс
class CarProcess
{
public:
    /// @brief Основной цикл работы процесса машины. Выполняет все этапы гонки: ожидает старт через файловую блокировку, отправляет прогресс в очередь сообщений, отмечается в барьере и ждёт разрешения на переход к следующему этапу.
    /// @param carId Идентификатор машины (0..kCarCount-1). Используется для маркировки сообщений и индексации в массиве барьера.
    /// @param progressQueueId Идентификатор очереди сообщений System V. Через неё машина отправляет арбитру обновления прогресса и сигнал о завершении этапа.
    /// @param shared Указатель на сегмент разделяемой памяти (SharedState). Используется для барьерной синхронизации: - arrivedStage[carId] — отметка о достижении барьера, - releaseStage — разрешение от арбитра продолжить выполнение. 
    /// @param inheritedStageLockFds Дескрипторы файлов этапов, унаследованные после fork(). В дочернем процессе должны быть закрыты, чтобы корректно работала блокировка flock при ожидании старта этапа.
    void Run(int carId, int progressQueueId, SharedState* shared,
             const std::vector<int>& inheritedStageLockFds)
    {
        // закрываем унаследованные fd лок-файлов
        // дочерний процесс забрал все открытые fd родителя
        for (int fd : inheritedStageLockFds)
        {
            if (fd >= 0) ::close(fd);
        }

        // настройка сида рандома для каждой машины своё
        std::mt19937 rng(static_cast<unsigned>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
            ^ (carId * 0x9e3779b9u)));
        
        // настройка генераторов
        std::uniform_int_distribution<int> stepDist(1, 10);
        std::uniform_int_distribution<int> sleepDist(100, 300);

        for (int stage = 1; stage <= cfg::kStageCount; ++stage)
        {
            // Ждём старт этапа через файл-блокировку 
            // Арбитр держит LOCK_EX. Мы пытаемся взять LOCK_SH и блокируемся,
            // пока арбитр не снимет эксклюзивную блокировку.
            WaitStageStart(stage);

            // начало этапа
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

                // имитация движения
                usleep(static_cast<useconds_t>(sleepDist(rng) * 1000));
            }

            ProgressMessage done{};
            done.carId = carId;
            done.distance = cfg::kFinishDistance;
            done.finished = 1;

            if (msgsnd(progressQueueId, &done, sizeof(done) - sizeof(long), 0) == -1)
                DieSys("msgsnd(done)");

            // Барьер: отмечаемся 
            shared->arrivedStage[carId] = stage; 

            // Ждём, пока арбитр разрешит переход дальше
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

// арбитр
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

        // Создание очереди сообщений
        progressQueueId_ = msgget(msgKey, IPC_CREAT | 0666);
        if (progressQueueId_ == -1) DieSys("msgget");

        // Создание shared memory
        shmId_ = shmget(shmKey, sizeof(SharedState), IPC_CREAT | 0666);
        if (shmId_ == -1) DieSys("shmget");

        // Подключение shared memory
        shared_ = static_cast<SharedState*>(shmat(shmId_, nullptr, 0));
        if (shared_ == reinterpret_cast<void*>(-1)) DieSys("shmat");

        // Инициализация барьера
        *shared_ = SharedState{};

        // До старта: блокируем все этапы.
        LockAllStagesExclusive();
    }

    ~RaceController()
    {
        // снять блокировки 
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

            // Копим результаты этапа для финальной сводки
            CaptureStageResults(stage);

            // Оставляем вывод промежуточных результатов сразу после этапа
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

        // В конце выводим сводку по всем этапам + финальные результаты
        ShowAllStageStandings();
        ShowFinalStandings();

        for (pid_t pid : carPids_)
            waitpid(pid, nullptr, 0);
    }

private:
    struct CarView
    {
        int distance{0};
        int stagePlace{0};
        int totalScore{0};   
        bool finished{false};
    };

    /// Снимок результатов этапа, чтобы вывести в конце вместе с финалом
    struct StageSnapshot
    {
        /// placeToCarNo[place] = номер машины (1..kCarCount) на данном месте
        std::array<int, cfg::kCarCount> placeToCarNo{};
        /// carPlace[carIdx] = место (1..kCarCount) данной машины на этапе
        std::array<int, cfg::kCarCount> carPlace{};
        /// totalScoreAfterStage[carIdx] = сумма очков у машины после этапа
        std::array<int, cfg::kCarCount> totalScoreAfterStage{};
        bool valid{false};
    };

    // Блокировка всех этапов
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
        // данный дескриптор этапа больше не нужен
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

        // снимаем эксклюзивную блокировку с файла этапа
        UnlockStage(stage);

        while (!AllCarsFinished())
        {
            DrainProgressQueue();
            RenderFrame();
            usleep(cfg::kRenderSleepUs);
        }

        DrainProgressQueue();

        // Барьер: ждём arrivedStage
        while (!AllCarsArrivedBarrier(stage))
            usleep(cfg::kWaitSleepUs);

        // Разрешаем всем продолжать
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

        // обнуление ошиьки при пустой очереди
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

    /// Сохраняем результаты этапа в stageResults_ для финальной сводки
    void CaptureStageResults(int stage)
    {
        const int idx = stage - 1;
        if (idx < 0 || idx >= cfg::kStageCount) DieSys("CaptureStageResults: bad stage");

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

        StageSnapshot snap{};
        for (int place = 0; place < cfg::kCarCount; ++place)
        {
            const CarView* car = order[place];
            const int carIdx = IndexOf(car);
            const int carNo = carIdx + 1;

            snap.placeToCarNo[place] = carNo;
            snap.carPlace[carIdx] = place + 1;
        }

        for (int i = 0; i < cfg::kCarCount; ++i)
            snap.totalScoreAfterStage[i] = cars_[i].totalScore;

        snap.valid = true;
        stageResults_[idx] = snap;
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

    /// Выводим накопленные результаты всех этапов в конце
    void ShowAllStageStandings() const
    {
        WriteLine("\n=== Результаты этапов (сводка) ===");

        for (int stage = 1; stage <= cfg::kStageCount; ++stage)
        {
            const int idx = stage - 1;
            const StageSnapshot& snap = stageResults_[idx];

            WriteLine("\nРезультаты этапа ", stage, ":");

            if (!snap.valid)
            {
                WriteLine("(нет данных, что-то пошло не так)");
                continue;
            }

            for (int place = 0; place < cfg::kCarCount; ++place)
            {
                const int carNo = snap.placeToCarNo[place];
                const int carIdx = carNo - 1;
                WriteLine("Место ", place + 1, ": Машина ", carNo,
                          " (Очки после этапа: ", snap.totalScoreAfterStage[carIdx], ")");
            }
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

    // накопленные результаты этапов
    std::array<StageSnapshot, cfg::kStageCount> stageResults_{};
};

int main()
{
    RaceController race;
    race.SpawnCars();
    race.RunRace();
    return 0;
}