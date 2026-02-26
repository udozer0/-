#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <thread>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// 1) Доменные типы (бензин, параметры, настройки)
enum GasType : int { AI76 = 0, AI92 = 1, AI95 = 2 };

static inline std::string_view ToString(GasType t) {
    switch (t) {
        case AI76: return "АИ76";
        case AI92: return "АИ92";
        case AI95: return "АИ95";
    }
    return "НЕИЗВЕСТНО";
}

struct Params {
    double mean{};   // среднее (в секундах)
    double stddev{}; // стандартное отклонение (в секундах)
};

struct Station {
    int id{};        // порядковый номер станции
    GasType type{};  // какой бензин обслуживает
    Params handle{}; // параметры обслуживания (сек)
};

struct Settings {
    std::vector<Station> stations_params{};
    Params create{};                 // параметры генерации заявок (сек)
    int number_message_queue{};      // размер очереди
    int requests{150};               // количество заявок 
};

static inline std::string Trim(std::string s) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

static inline std::string StripInlineComment(std::string s) {
    // Комментарии начинаются с '#' или ';'
    auto pos1 = s.find('#');
    auto pos2 = s.find(';');
    auto pos = std::min(pos1 == std::string::npos ? s.size() : pos1,
                        pos2 == std::string::npos ? s.size() : pos2);
    s.resize(pos);
    return s;
}

static inline bool StartsWith(const std::string& s, const char* prefix) {
    const size_t n = std::char_traits<char>::length(prefix);
    return s.size() >= n && std::equal(prefix, prefix + n, s.begin());
}

static inline bool ParseInt(const std::string& s, int& out) {
    try {
        std::string t = Trim(s);
        size_t idx = 0;
        long v = std::stol(t, &idx);
        if (idx != t.size()) return false;
        out = static_cast<int>(v);
        return true;
    } catch (...) {
        return false;
    }
}

static inline bool ParseDouble(const std::string& s, double& out) {
    try {
        std::string t = Trim(s);
        size_t idx = 0;
        out = std::stod(t, &idx);
        return idx == t.size();
    } catch (...) {
        return false;
    }
}

static inline bool SplitKeyValue(const std::string& line, std::string& key, std::string& value) {
    auto eq = line.find('=');
    if (eq == std::string::npos) return false;
    key = Trim(line.substr(0, eq));
    value = Trim(line.substr(eq + 1));
    return !key.empty();
}

static inline std::optional<Settings> GetSettings(const std::string& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;

    Settings settings{};
    std::unordered_map<int, Station> stations;

    enum class SectionKind { Root, Create, Station };
    SectionKind section = SectionKind::Root;
    int current_station_id = -1;

    auto require_station = [&](int id) -> Station& {
        auto it = stations.find(id);
        if (it == stations.end()) {
            Station st{};
            st.id = id;
            it = stations.emplace(id, st).first;
        }
        return it->second;
    };

    std::string raw;
    int line_no = 0;

    while (std::getline(in, raw)) {
        ++line_no;
        std::string line = Trim(StripInlineComment(raw));
        if (line.empty()) continue;

        // [section]
        if (line.size() >= 2 && line.front() == '[' && line.back() == ']') {
            std::string name = Trim(line.substr(1, line.size() - 2));

            if (name == "create") {
                section = SectionKind::Create;
                current_station_id = -1;
                continue;
            }

            if (StartsWith(name, "station")) {
                auto rest = Trim(name.substr(std::string("station").size()));
                int id = 0;
                if (!ParseInt(rest, id) || id <= 0) {
                    std::cerr << "Ошибка конфига (строка " << line_no << "): неверный station id\n";
                    return std::nullopt;
                }
                section = SectionKind::Station;
                current_station_id = id;
                (void)require_station(id);
                continue;
            }

            std::cerr << "Ошибка конфига (строка " << line_no << "): неизвестная секция [" << name << "]\n";
            return std::nullopt;
        }

        std::string key, value;
        if (!SplitKeyValue(line, key, value)) {
            std::cerr << "Ошибка конфига (строка " << line_no << "): ожидается key=value\n";
            return std::nullopt;
        }

        auto assign_params = [&](Params& p, const std::string& k, const std::string& v) -> bool {
            if (k == "mean")   return ParseDouble(v, p.mean);
            if (k == "stddev") return ParseDouble(v, p.stddev);
            return false;
        };

        if (section == SectionKind::Root) {
            if (key == "number_message_queue") {
                int v = 0;
                if (!ParseInt(value, v) || v <= 0) {
                    std::cerr << "Ошибка конфига (строка " << line_no << "): number_message_queue должен быть > 0\n";
                    return std::nullopt;
                }
                settings.number_message_queue = v;
                continue;
            }
            if (key == "requests") {
                int v = 0;
                if (!ParseInt(value, v) || v <= 0) {
                    std::cerr << "Ошибка конфига (строка " << line_no << "): requests должен быть > 0\n";
                    return std::nullopt;
                }
                settings.requests = v;
                continue;
            }

            std::cerr << "Ошибка конфига (строка " << line_no << "): неизвестный ключ " << key << "\n";
            return std::nullopt;
        }

        if (section == SectionKind::Create) {
            if (assign_params(settings.create, key, value)) continue;
            std::cerr << "Ошибка конфига (строка " << line_no << "): неизвестный ключ в [create] " << key << "\n";
            return std::nullopt;
        }

        if (section == SectionKind::Station) {
            if (current_station_id <= 0) return std::nullopt;
            Station& st = require_station(current_station_id);

            if (key == "type") {
                int t = 0;
                if (!ParseInt(value, t) || t < 0 || t > 2) {
                    std::cerr << "Ошибка конфига (строка " << line_no << "): type должен быть 0..2\n";
                    return std::nullopt;
                }
                st.type = static_cast<GasType>(t);
                continue;
            }
            if (key == "handle.mean") {
                if (!ParseDouble(value, st.handle.mean)) {
                    std::cerr << "Ошибка конфига (строка " << line_no << "): handle.mean неверное число\n";
                    return std::nullopt;
                }
                continue;
            }
            if (key == "handle.stddev") {
                if (!ParseDouble(value, st.handle.stddev)) {
                    std::cerr << "Ошибка конфига (строка " << line_no << "): handle.stddev неверное число\n";
                    return std::nullopt;
                }
                continue;
            }

            std::cerr << "Ошибка конфига (строка " << line_no << "): неизвестный ключ в [station] " << key << "\n";
            return std::nullopt;
        }
    }

    settings.stations_params.clear();
    settings.stations_params.reserve(stations.size());
    for (auto& kv : stations) settings.stations_params.push_back(std::move(kv.second));
    std::sort(settings.stations_params.begin(), settings.stations_params.end(),
              [](const Station& a, const Station& b){ return a.id < b.id; });

    if (settings.number_message_queue <= 0) {
        std::cerr << "Ошибка конфига: не задан number_message_queue\n";
        return std::nullopt;
    }
    if (settings.stations_params.empty()) {
        std::cerr << "Ошибка конфига: не задано ни одной станции\n";
        return std::nullopt;
    }
    // Дефолтный
    if (settings.create.mean <= 0) {
        settings.create.mean = 1.0;
        settings.create.stddev = 0.5;
    }
    return settings;
}

// 3) IPC структуры (shared memory + semaphores)

// Машина в очереди
struct Car {
    int id;
    int gas;            // 0..2
    long long ts_ms;    // для протокола
};

// Заголовок shared memory
struct ShmHeader {
    int capacity;   // емкость очереди
    int size;       // текущий размер очереди
    int finished;   // 0/1: генератор закончил
};

// Указатель на массив машин сразу после заголовка
static inline Car* ShmCars(ShmHeader* h) {
    return reinterpret_cast<Car*>(reinterpret_cast<char*>(h) + sizeof(ShmHeader));
}

// Индексы семафоров в наборе semget
enum SemIndex : unsigned short {
    SEM_MUTEX = 0,      // бинарный семафор-мьютекс (защита критической секции)
    SEM_EMPTY = 1,      // сколько свободных мест в очереди
    SEM_FUEL_76 = 2,    // сколько машин АИ76 в очереди
    SEM_FUEL_92 = 3,    // сколько машин АИ92 в очереди
    SEM_FUEL_95 = 4,    // сколько машин АИ95 в очереди
    SEM_COUNT = 5
};

// Для semctl SETVAL
union semun {
    int val;
    struct semid_ds* buf;
    unsigned short* array;
};

static inline void DieSys(const char* what) {
    std::cerr << what << " не сработало: " << std::strerror(errno) << "\n";
    std::exit(1);
}

// Операция над семафором: -1 (P) ждать, +1 (V) оповещать
static inline void SemOp(int semid, unsigned short semnum, short delta) {
    sembuf op{};
    op.sem_num = semnum;
    op.sem_op  = delta;
    op.sem_flg = 0;
    if (semop(semid, &op, 1) == -1) DieSys("semop");
}

// НЕБЛОКИРУЮЩАЯ попытка операции над семафором.
// Возвращает true, если получилось. Если ресурса нет — false (errno == EAGAIN).
static inline bool SemTryOp(int semid, unsigned short semnum, short delta) {
    sembuf op{};
    op.sem_num = semnum;
    op.sem_op  = delta;
    op.sem_flg = IPC_NOWAIT;
    if (semop(semid, &op, 1) == 0) return true;

    if (errno == EAGAIN) return false;
    DieSys("semop (try)");
    return false;
}

static inline unsigned short FuelSemIndex(int gas) {
    switch (gas) {
        case 0: return SEM_FUEL_76;
        case 1: return SEM_FUEL_92;
        case 2: return SEM_FUEL_95;
        default: return SEM_FUEL_95;
    }
}

static inline long long NowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// Добавить машину в очередь
static inline void Enqueue(ShmHeader* h, const Car& car) {
    Car* cars = ShmCars(h);
    cars[h->size] = car;
    h->size++;
}

// Забрать первую машину нужного топлива
static inline bool DequeueFirstMatching(ShmHeader* h, int gas, Car& out) {
    Car* cars = ShmCars(h);
    for (int i = 0; i < h->size; ++i) {
        if (cars[i].gas == gas) {
            out = cars[i];
            for (int j = i; j < h->size - 1; ++j) cars[j] = cars[j + 1];
            h->size--;
            return true;
        }
    }
    return false;
}

// 4) Процесс станции (потребитель)
static constexpr const char* PROTOCOL_DIR = "protocols";

static void StationProcess(const Station& st, int semid, ShmHeader* shm) {
    std::filesystem::create_directory(PROTOCOL_DIR);

    std::string fname = std::string(PROTOCOL_DIR) + "/station_" +
                        std::to_string(st.id) + "_" + std::string(ToString(st.type)) + ".log";
    std::ofstream log(fname, std::ios::out | std::ios::app);

    std::mt19937 gen(std::random_device{}());
    std::normal_distribution<double> dist(st.handle.mean, st.handle.stddev > 0 ? st.handle.stddev : 0.001);

    const int gas = static_cast<int>(st.type);
    const unsigned short fuelSem = FuelSemIndex(gas);

    log << "Станция " << st.id << " (" << ToString(st.type) << ") запущена.\n";
    log.flush();

    while (true) {
        // Ожидание машины
        SemOp(semid, fuelSem, -1);

        SemOp(semid, SEM_MUTEX, -1);

        Car car{};
        const bool got = DequeueFirstMatching(shm, gas, car);
        const bool finished = (shm->finished != 0);

        SemOp(semid, SEM_MUTEX, +1);

        if (!got) {
            // Если генератор закончил - выходим.
            if (finished) break;
            continue;
        }

        // Освобождаем место в очереди
        SemOp(semid, SEM_EMPTY, +1);

        const double sec = std::max(0.0, dist(gen));

        std::cout << "Станция " << st.id << " (" << ToString(st.type) << ") взяла машину #" << car.id << "\n";
        log << "НАЧАЛО: станция=" << st.id
            << " машина=" << car.id
            << " топливо=" << ToString(static_cast<GasType>(car.gas))
            << " время_постановки_ms=" << car.ts_ms
            << " обслуживание_сек=" << sec
            << "\n";
        log.flush();

        // Обслуживаем
        std::this_thread::sleep_for(std::chrono::duration<double>(sec));

        log << "КОНЕЦ:  станция=" << st.id
            << " машина=" << car.id
            << " топливо=" << ToString(static_cast<GasType>(car.gas))
            << "\n";
        log.flush();
    }

    std::cout << "Станция " << st.id << " (" << ToString(st.type) << ") завершилась.\n";
    log << "Станция завершилась.\n";
    log.flush();
}

// 5) Производитель (генератор заявок) - выполняется в родителе
static void ProducerProcess(const Settings& settings, int semid, ShmHeader* shm) {
    std::filesystem::create_directory(PROTOCOL_DIR);

    // лог успешных добавлений в очередь
    std::ofstream qlog(std::string(PROTOCOL_DIR) + "/queue.log", std::ios::out | std::ios::app);
    // лог отказов
    std::ofstream rlog(std::string(PROTOCOL_DIR) + "/rejects.log", std::ios::out | std::ios::app);

    std::mt19937 gen(std::random_device{}());
    std::normal_distribution<double> genDist(settings.create.mean,
                                            settings.create.stddev > 0 ? settings.create.stddev : 0.001);

    // Распределение топлива 2 2 1
    std::uniform_int_distribution<int> fuelDist(0, 4);
    auto pickFuel = [&]() -> int {
        int v = fuelDist(gen);
        return (v < 2) ? 0 : (v < 4 ? 1 : 2);
    };

    std::cout << "Генератор: заявок=" << settings.requests
              << ", очередь=" << settings.number_message_queue << "\n";

    for (int id = 0; id < settings.requests; ++id) {
        const double sleep_sec = std::max(0.0, genDist(gen));
        std::this_thread::sleep_for(std::chrono::duration<double>(sleep_sec));

        const int gas = pickFuel();
        Car car{ id, gas, NowMs() };

        // Пытаемся занять место в очереди. Если мест нет — отклоняем заявку.
        if (!SemTryOp(semid, SEM_EMPTY, -1)) {
            std::cout << "ОТКАЗ: машина #" << car.id << " (" << ToString(static_cast<GasType>(gas))
                      << ") — очередь заполнена\n";

            rlog << "ОТКАЗ: машина=" << car.id
                 << " топливо=" << ToString(static_cast<GasType>(car.gas))
                 << " ts_ms=" << car.ts_ms
                 << " причина=очередь_заполнена"
                 << "\n";
            rlog.flush();
            continue;
        }

        // Критическая секция: добавляем в shared memory очередь
        SemOp(semid, SEM_MUTEX, -1);
        Enqueue(shm, car);
        int qsize = shm->size;
        SemOp(semid, SEM_MUTEX, +1);

        // Сигнал колонке
        SemOp(semid, FuelSemIndex(gas), +1);

        std::cout << "Новая машина #" << car.id << " (" << ToString(static_cast<GasType>(gas))
                  << "), размер очереди=" << qsize << "\n";

        qlog << "ДОБАВЛЕНО: машина=" << car.id
             << " топливо=" << ToString(static_cast<GasType>(car.gas))
             << " ts_ms=" << car.ts_ms
             << " размер_очереди=" << qsize
             << "\n";
        qlog.flush();
    }

    // Генератор закончил
    SemOp(semid, SEM_MUTEX, -1);
    shm->finished = 1;
    SemOp(semid, SEM_MUTEX, +1);

    // Сигнал всем станциям, чтобы они могли корректно выйти
    int cnt76 = 0, cnt92 = 0, cnt95 = 0;
    for (const auto& st : settings.stations_params) {
        if (st.type == AI76) cnt76++;
        else if (st.type == AI92) cnt92++;
        else if (st.type == AI95) cnt95++;
    }
    for (int i = 0; i < cnt76; ++i) SemOp(semid, SEM_FUEL_76, +1);
    for (int i = 0; i < cnt92; ++i) SemOp(semid, SEM_FUEL_92, +1);
    for (int i = 0; i < cnt95; ++i) SemOp(semid, SEM_FUEL_95, +1);

    std::cout << "Генератор: завершил генерацию.\n";
    qlog << "ГЕНЕРАТОР: завершил генерацию.\n";
    qlog.flush();
}

static constexpr const char* FTOK_PATH = "ipc_keyfile_gaslab";

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Использование: " << argv[0] << " ./config.conf\n";
        return 1;
    }

    auto settingsOpt = GetSettings(argv[1]);
    if (!settingsOpt) {
        std::cerr << "Не удалось прочитать конфиг: " << argv[1] << "\n";
        return 1;
    }
    Settings settings = *settingsOpt;

    std::filesystem::remove_all(PROTOCOL_DIR);
    std::filesystem::create_directory(PROTOCOL_DIR);

    {
        std::ofstream f(FTOK_PATH);
    }

    const key_t key = ftok(FTOK_PATH, 1);
    if (key == -1) DieSys("ftok");

    // Создаём набор семафоров
    const int semid = semget(key, SEM_COUNT, IPC_CREAT | 0666);
    if (semid == -1) DieSys("semget");

    // Создаём shared memory под заголовок + массив Car[capacity]
    const int cap = settings.number_message_queue;
    const size_t shmSize = sizeof(ShmHeader) + sizeof(Car) * static_cast<size_t>(cap);

    const int shmid = shmget(key, shmSize, IPC_CREAT | 0666);
    if (shmid == -1) DieSys("shmget");

    void* mem = shmat(shmid, nullptr, 0);
    if (mem == (void*)-1) DieSys("shmat");

    auto* shm = reinterpret_cast<ShmHeader*>(mem);

    // Инициализация shared memory
    shm->capacity = cap;
    shm->size = 0;
    shm->finished = 0;

    // Инициализация семафоров
    semun arg{};

    // mutex = 1
    arg.val = 1;
    if (semctl(semid, SEM_MUTEX, SETVAL, arg) == -1) DieSys("semctl mutex");

    // empty = capacity (свободные места)
    arg.val = cap;
    if (semctl(semid, SEM_EMPTY, SETVAL, arg) == -1) DieSys("semctl empty");

    // fuel semaphores = 0
    arg.val = 0;
    if (semctl(semid, SEM_FUEL_76, SETVAL, arg) == -1) DieSys("semctl fuel76");
    if (semctl(semid, SEM_FUEL_92, SETVAL, arg) == -1) DieSys("semctl fuel92");
    if (semctl(semid, SEM_FUEL_95, SETVAL, arg) == -1) DieSys("semctl fuel95");

    // Создание станций (процессов)
    std::vector<pid_t> stationPids;
    stationPids.reserve(settings.stations_params.size());

    for (const auto& st : settings.stations_params) {
        pid_t pid = fork();
        if (pid == -1) DieSys("fork");

        if (pid == 0) {
            // Дочерний процесс: станция
            StationProcess(st, semid, shm);

            // Отцепиться от shared memory и выйти
            shmdt(mem);
            _exit(0);
        }

        stationPids.push_back(pid);
    }

    // Родитель: генератор
    ProducerProcess(settings, semid, shm);

    // Ждём завершения всех станций
    for (pid_t pid : stationPids) {
        int status = 0;
        waitpid(pid, &status, 0);
    }

    // Родитель чистит IPC
    shmdt(mem);

    // удалить сегмент shared memory из системы
    if (shmctl(shmid, IPC_RMID, nullptr) == -1) DieSys("shmctl IPC_RMID");
    // удалить семафоры из системы
    if (semctl(semid, 0, IPC_RMID) == -1) DieSys("semctl IPC_RMID");

    std::filesystem::remove(FTOK_PATH);

    std::cout << "Готово. Протоколы лежат в ./" << PROTOCOL_DIR << "/\n";
    return 0;
}
