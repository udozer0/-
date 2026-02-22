Окей. Ты хочешь “пояснение по каждой строчке так, чтобы даже обезьяна не докопалась”. Значит будет **буквально построчно**, с объяснением “что делает”, “зачем”, “что будет если убрать/ошибиться”, и где тут **косяки/опасные места**. Пристегни мозг, если он у тебя не на липучке.

Ниже я разобью на логические файлы, потому что у тебя это всё в одной простыне, но по смыслу это разные модули.

---

## 1) `main.cpp` (обработка Ctrl+C и ожидание завершения)

```cpp
#include <condition_variable>
```

Подключает заголовок стандартной библиотеки C++, где объявлены:

* `std::condition_variable` (условная переменная)
* связанные штуки для ожидания/пробуждения потоков
  Без этого `std::condition_variable notify;` не скомпилируется.

```cpp
#include "loop.hpp"
```

Подключает твой заголовок `loop.hpp`, где (судя по коду ниже) объявлен класс `MainLoop` и, возможно, `Settings`.
Кавычки `"..."` означают “сначала ищи рядом/в include-путях проекта”.

```cpp
std::condition_variable notify;
```

Глобальная условная переменная.

* Смысл: один поток может **ждать**, пока другой его **разбудит**.
* Здесь main-поток будет ждать, пока не придёт сигнал.

**Важно:** условная переменная сама по себе не хранит “состояние”. Она только “будит”. Если разбудили до того как ты начал ждать, ты это событие можешь тупо потерять. Обычно рядом хранят флаг (bool) и ждут по предикату. У тебя этого нет.

```cpp
std::mutex cond_lock;
```

Глобальный мьютекс, который используют вместе с `notify`.

* `condition_variable::wait()` требует `std::unique_lock<std::mutex>`, чтобы корректно “отпустить мьютекс и уснуть” атомарно.

---

### Обработчик сигнала

```cpp
void signalHandler(int signum)
```

Функция-обработчик сигнала.

* `signum` = номер сигнала (например `SIGINT`), но ты его не используешь.

```cpp
{
    std::cout << "Завершаем..." << std::endl;
```

Печатает сообщение о завершении.

**Очень важный косяк:**
Внутри обработчика сигнала **нельзя безопасно** делать почти ничего из C++ iostream (`std::cout`) и вообще кучу стандартных вещей. Это не async-signal-safe. Может зависнуть или сломаться странно и редко, что хуже всего.

```cpp
    notify.notify_all();
```

Пытается разбудить все потоки, которые ждут на `notify`.

**Второй большой косяк:**
`notify_all()` тоже не обязан быть async-signal-safe. То есть вызывать его из signal handler тоже потенциально опасно.

Правильнее: в обработчике сигнала менять атомарный флаг или писать в pipe/eventfd, а уже обычный поток пусть будит condition_variable.

```cpp
}
```

Конец обработчика.

---

### `main`

```cpp
int main(int argc, char **argv)
```

Точка входа программы.

* `argc` = количество аргументов командной строки
* `argv` = массив строк-аргументов (`argv[0]` обычно имя программы)

```cpp
{
    std::signal(SIGINT, signalHandler);
```

Регистрирует обработчик `signalHandler` для сигнала `SIGINT` (обычно Ctrl+C).

**Заметка:** `std::signal` из `<csignal>` (а у тебя тут в этом кусочке не подключён, но ниже в другом месте есть `<csignal>`). Компиляция зависит от того, как у тебя реально разнесены файлы.

```cpp
    Settings settings;
```

Создаёт объект настроек.

* Если у `Settings` нет явного конструктора, это просто “пустая” структура, поля будут дефолтно инициализированы (как в твоём `Settings` в settings.hpp: `stations_params{}` пустой, `create{}` нули, `number_message_queue{}` = 0).

```cpp
    if (argc > 0)
```

Проверяет, что аргументов больше нуля.

**Лютый логический косяк:**
`argc` **всегда ≥ 1**, потому что `argv[0]` существует (имя программы).
Ты хотел проверить наличие `argv[1]`, значит нужно `argc > 1`.

```cpp
    {
        const auto path_to_settings = std::string(argv[1]);
```

Берёт `argv[1]` и превращает в `std::string`.

**Если argc == 1**, то `argv[1]` не существует. Это UB (undefined behavior). Может упасть, может читать мусор, может “вроде работает” пока не перестанет.

```cpp
        if (std::filesystem::exists(path_to_settings))
```

Проверяет, существует ли файл/путь.

```cpp
        {
            const auto set = GetSettings(path_to_settings);
```

Вызывает `GetSettings`, который возвращает `std::optional<Settings>`:

* если распарсилось: optional содержит Settings
* если нет: optional пустой

```cpp
            if (set)
            {
                settings = set.value();
```

`if (set)` проверяет, что optional не пустой.
`set.value()` достаёт Settings (если пустой, кинет исключение, но ты проверил).

```cpp
                std::cout << "Прочитана конфигурация " << path_to_settings << std::endl;
```

Пишет лог, что конфиг прочитан.

```cpp
            }
        }
    }
```

Закрываем вложенные if.

```cpp
    MainLoop loop(settings);
```

Создаёт объект `MainLoop`.

* Это запускает кучу логики в конструкторе: fork’и, потоки, сигналы, протоколирование.

**Важно:** `loop` живёт до конца `main`. Когда `main` заканчивается, вызовется `~MainLoop()`, где ты пытаешься стопнуть процессы и потоки.

```cpp
    std::unique_lock<std::mutex> lock(cond_lock);
```

Создаёт `unique_lock`, который захватывает `cond_lock` (мьютекс) сразу.

* Это нужно для `notify.wait(lock)`.

```cpp
    notify.wait(lock);
```

Засыпает, пока кто-то не вызовет `notify.notify_*()`.

**Критично:**
Так ждать нельзя “по-хорошему”, потому что:

* wait может проснуться “ложно” (spurious wakeup)
* событие может быть “пропущено”
  Правильный паттерн: `notify.wait(lock, []{ return stop_flag; });`

```cpp
}
```

Конец `main`.

---

## 2) `loop.hpp / loop.cpp` (MainLoop: fork + поток + сигналы + очередь)

### Инклюды

```cpp
#include "messages.hpp"
#include "queue.hpp"
#include "settings.hpp"
```

Твои заголовки:

* сообщения (`Message`, `GasType`, `Status`)
* очередь (`Queue`)
* настройки (`Settings`, `Station`, `Params`, `GetSettings`)

```cpp
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
```

Стандартые и POSIX заголовки:

* `<atomic>`: `std::atomic<bool>` для безопасного флага `run_`
* `<chrono>`: миллисекунды и sleep
* `<csignal>`: сигналы `SIGUSR1`, `sigemptyset`, `sigwait` и т.п.
* `<filesystem>`: папки/файлы
* `<fstream>`: файлы протоколов
* `<random>`: генератор и распределения
* `<sys/wait.h>`: `waitpid`
* `<unistd.h>`: `fork`, `getpid`, `killpg`
* `<thread>`: `std::thread`
* и т.д.

---

### Объявление класса

```cpp
class MainLoop
{
public:
```

Класс, который инкапсулирует “основную работу” системы: создание заявок (produce) и обработку станциями (consume).

---

### Конструктор

```cpp
    MainLoop(Settings settings) : settings_(settings), queue_(settings.number_message_queue)
```

Конструктор принимает Settings **по значению** (копией).
В списке инициализации:

* `settings_(settings)` копирует настройки в поле класса
* `queue_(settings.number_message_queue)` создаёт очередь нужной ёмкости

**Важно:** если `number_message_queue` = 0 (а у тебя по дефолту так), очередь будет ёмкостью 0 и всё будет сразу REJECTED.

```cpp
    {
        // Обновляем протоколы
        if (std::filesystem::exists(protocol_dir))
        {
            std::filesystem::remove_all(protocol_dir);
        }
```

Если папка `protocols` существует, удаляет её целиком со всем содержимым.

**Осторожно:** `remove_all` реально удаляет. Ошибся с именем папки и привет, минус важные файлы.

```cpp
        std::filesystem::create_directory(protocol_dir);
```

Создаёт папку `protocols`.

```cpp
        queue_protocol_.open(std::filesystem::path(protocol_dir) / "queue");
```

Открывает файл `protocols/queue` на запись (по умолчанию trunc).
Этот файл используется как общий лог очереди: “новая заявка”, “станция обработала”.

---

#### Настройка сигналов SIGUSR1

```cpp
        sigemptyset(&set_);
```

Очищает набор сигналов `set_` (теперь он пустой).

```cpp
        sigaddset(&set_, SIGUSR1);
```

Добавляет `SIGUSR1` в набор. Это сигнал “появилась заявка”.

```cpp
        pthread_sigmask(SIG_BLOCK, &set_, nullptr);
```

Блокирует сигналы из `set_` (то есть SIGUSR1) **для текущего потока** (и для потоков, которые будут созданы после, если маска наследуется).
Смысл: вместо асинхронного обработчика, ты ждёшь сигнал синхронно через `sigwait`.

**Это нормально**, `sigwait` как раз работает с заблокированными сигналами.

---

```cpp
        run_ = true;
```

Ставишь флаг “работаем”. Он атомарный, то есть разные потоки могут читать/писать без data race.

```cpp
        parent_pid_ = getpid();
```

Запоминаешь PID процесса-родителя (главного процесса), чтобы потом посылать сигнал всей группе.

---

#### fork по станциям

```cpp
        for (const auto &station : settings_.stations_params)
        {
            const auto pid = fork();
```

Для каждой станции создаёшь новый процесс через `fork()`.

* В родителе `pid` будет PID ребёнка (>0)
* В ребёнке `pid` будет 0
* При ошибке `pid` = -1 (у тебя не обработано)

```cpp
            if (pid == 0)
            {
                setpgid(0, parent_pid_);
```

Это код, который выполнится **в ребёнке**.

`setpgid(0, parent_pid_)`:

* “0” означает “текущий процесс”
* `parent_pid_` как PGID: ты пытаешься поместить ребёнка в группу процессов, идентификатор группы = PID родителя.

Смысл: потом `killpg(parent_pid_, SIGUSR1)` будет слать сигнал всем процессам группы.

**Опасный момент:** если что-то пошло не так с группами, сигналы не туда улетят.

```cpp
                thread_ = std::thread{&MainLoop::Consume, this, station};
```

Внутри ребёнка создаёшь поток, который будет выполнять `Consume(station)`.

**Очень важная странность:** у тебя дети это процессы, и внутри каждого процесса ты ещё поднимаешь поток. Это можно, но:

* `fork` + `std::thread` требуют осторожности: после fork в дочернем процессе безопасно вызывать далеко не всё, если в момент fork были другие потоки в родителе. Здесь fork делается до запуска producer-потока, так что шанс меньше, но архитектура всё равно хрупкая.

```cpp
                return;
```

Выход из конструктора в дочернем процессе.
То есть у ребёнка объект `MainLoop` создан, у него запущен поток Consume, и дальше управление вернётся в `main`, где `main` почти сразу усыпляется на condition_variable.

---

```cpp
            processes_.push_back(pid);
```

Это выполнится в родителе (где pid > 0): сохраняешь PID ребёнка в вектор.

---

#### Запуск producer-потока в родителе

```cpp
        thread_ = std::thread{&MainLoop::Produce, this, settings_.create};
```

В родителе запускаешь поток `Produce`, который генерит заявки.

**Проблема:** переменная `thread_` используется и у детей (Consume) и у родителя (Produce). В каждом процессе это свой экземпляр памяти, но логика деструктора всё равно у тебя подозрительная.

---

### Деструктор

```cpp
    ~MainLoop()
    {
        run_ = false;
```

Ставишь флаг “стоп”.

**Но:** дети сидят в `sigwait`, они не проснутся только от изменения `run_`. Им нужен сигнал, иначе они будут ждать бесконечно.

```cpp
        for (auto &i : processes_)
        {
            int status;
            waitpid(i, &status, 0);
        }
```

Ждёшь завершения каждого дочернего процесса.

**Критическая проблема:** ты не отправляешь им команду завершиться (ни SIGTERM, ни SIGUSR1, ни что).
Если дети зависли в `sigwait`, родитель будет ждать их вечно.

```cpp
        thread_.join();
```

Ждёшь завершения producer-потока (в родителе) или consumer-потока (в ребёнке).

**В родителе:** Produce может тоже висеть, пока `run_` не проверит. Он проверяет `while(run_)`, ок, но sleep может задержать завершение.

**В ребёнке:** join будет ждать consumer-поток, который может висеть в `sigwait`, если никто не посылает SIGUSR1.

---

### Produce

```cpp
    void Produce(Params par)
```

Функция генерации заявок. Принимает параметры распределения времени генерации.

```cpp
        std::ofstream protocol{std::filesystem::path(protocol_dir) / "rejected"};
```

Открывает файл `protocols/rejected` для записи. Лог отклонённых заявок.

```cpp
        std::random_device rd;
        std::mt19937 gen(rd());
```

Создаёт генератор случайных чисел:

* `rd()` даёт seed
* `mt19937` это Mersenne Twister

```cpp
        int id = 0;
```

Счётчик ID сообщений.

```cpp
        while (run_)
        {
            const auto type = type_gas_(gen);
```

Пока работаем:

* выбираем случайный тип топлива из распределения `type_gas_` (0..2).

```cpp
            const auto time = GetRandomTime(par);
```

Получаем случайное время задержки (нормальное распределение mean/stddev).

```cpp
            std::this_thread::sleep_for(std::chrono::milliseconds(time));
```

Спим `time` миллисекунд, имитируем “появление заявки”.

**Если time отрицательный (нормальное распределение может дать отрицательное), это беда.**
`milliseconds(time)` с отрицательным значением формально допустим, но логика “спать отрицательно” не имеет смысла. Обычно clamp: `max(0, time)`.

```cpp
            Message mes;
            mes.gas_type = static_cast<GasType>(type);
            mes.id = id++;
```

Создаёшь сообщение:

* назначаешь тип топлива
* назначаешь уникальный id
* status по умолчанию EXPECTED (см. struct Message)

```cpp
            const auto added_mes = queue_.Push(mes);
```

Пытаешься положить сообщение в очередь (в shared memory).
`Push` возвращает сообщение (возможно со статусом REJECTED).

```cpp
            if (mes.status == REJECTED)
            {
                protocol << added_mes << std::endl;
            }
```

Если сообщение отклонено (очередь переполнена) пишешь в rejected-протокол.

**Тупой баг:** ты проверяешь `mes.status`, но `Push` меняет `elem.status`, да, но ты потом логируешь `added_mes`. Ок, но логичнее проверять `added_mes.status`, а не исходный `mes` (хотя тут они одинаковы по факту).

```cpp
            std::cout << "Новая заявка: " << mes << std::endl;
            queue_protocol_ << "Новая заявка: " << mes << std::endl;
```

Пишешь в консоль и в файл очереди.

```cpp
            killpg(parent_pid_, SIGUSR1);
```

Посылаешь сигнал SIGUSR1 **всем процессам группы** `parent_pid_`.
Это “эй, пришла новая заявка, проснитесь”.

---

### Consume

```cpp
    void Consume(Station station)
```

Обработчик заявок конкретной станции. Станция имеет:

* id
* type топлива, который она умеет
* Params handle (время обработки)

```cpp
        std::stringstream name_protocol{};
        name_protocol << ToString(station.type) << " " << getpid();
```

Собираешь имя файла протокола: например `AI92 12345`, где 12345 PID процесса станции.

```cpp
        std::ofstream protocol{std::filesystem::path(protocol_dir) / name_protocol.str()};
```

Открываешь файл протокола конкретной станции.

```cpp
        while (run_)
        {
            int sig;
            sigwait(&set_, &sig);
```

Пока работаем:

* ждёшь сигнал SIGUSR1 (он в `set_`) синхронно.
* `sig` получает номер принятого сигнала.

**Если нужно завершаться**, сигнала может не быть, и ты зависнешь тут навечно.

```cpp
            while (auto mes = queue_.Pop(station.type))
            {
```

Пока из очереди удаётся вытаскивать сообщения для этого типа топлива:

* `Pop` возвращает `std::optional<Message>`
* если optional не пустой, цикл выполняется

```cpp
                std::cout << "ID Station: " << station.id << " --> " << mes.value() << std::endl;
                queue_protocol_ << "ID Station: " << station.id << " --> " << mes.value() << std::endl;
```

Логируешь “взяли сообщение в работу”.

```cpp
                const auto time = GetRandomTime(station.handle);
                std::this_thread::sleep_for(std::chrono::milliseconds(time));
```

Имитируешь обработку, спишь.

Снова проблема с отрицательным временем.

```cpp
                mes->status = PROCESSED;
```

Меняешь статус сообщения на PROCESSED **в локальной копии optional**.
Это не обновляет сообщение в shared memory, потому что `Pop` уже удалил его из массива.

```cpp
                std::cout << "ID Station: " << station.id << " --> " << mes.value() << std::endl;
                protocol << "ID Station: " << station.id << " --> " << mes.value() << std::endl;
                queue_protocol_ << "ID Station: " << station.id << " --> " << mes.value() << std::endl;
```

Логируешь “обработали”. В протокол станции и в протокол очереди.

---

### GetRandomTime

```cpp
    int GetRandomTime(Params st)
```

Получает случайное время по нормальному распределению.

```cpp
        double mean = st.mean;
        double stddev = st.stddev;
```

Забирает параметры.

```cpp
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<double> dist(mean, stddev);
```

Создаёт генератор и нормальное распределение.

**Косяк производительности/качества:** ты создаёшь новый `random_device` и `mt19937` каждый вызов. Это дорого и иногда даёт плохой seed. Нормально держать `gen` как поле или `thread_local`.

```cpp
        return dist(gen);
```

Возвращает случайное число double, неявно приводится к int (обрезается).

---

### Поля класса

```cpp
    Queue queue_;
```

Очередь на shared memory.

```cpp
    std::atomic<bool> run_{false};
```

Флаг работы.

```cpp
    sigset_t set_;
    pid_t parent_pid_;
```

Набор сигналов (SIGUSR1) и PID “группы”.

```cpp
    std::thread thread_;
    std::vector<pid_t> processes_{};
```

Один поток (producer или consumer, зависит от процесса) и список PID дочерних процессов (только в родителе он заполнен).

```cpp
    std::ofstream queue_protocol_;
```

Файл общего протокола очереди.

```cpp
    Settings settings_;
    std::uniform_int_distribution<int> type_gas_{0, 2};
```

Настройки и распределение типов топлива.

---

## 3) `messages.hpp` (структуры сообщений)

```cpp
#ifndef MESSAGES_HPP
#define MESSAGES_HPP
```

Защита от повторного включения заголовка (include guard).

```cpp
#include <ostream>
#include <string_view>
```

Нужно для:

* `std::ostream` (оператор вывода)
* `std::string_view` (лёгкая строка-представление)

```cpp
enum GasType { AI76 = 0, AI92 = 1, AI95 = 2 };
```

Перечисление типов топлива. Значения 0..2 совпадают с твоим `uniform_int_distribution`.

```cpp
inline std::string_view ToString(GasType type)
```

Функция превращает `GasType` в строку.

```cpp
    switch (type) { ... }
```

Выбор строки по значению.

```cpp
    return "";
```

Если пришло невалидное значение (не 0..2) возвращает пустую строку.

---

```cpp
enum Status { PROCESSED = 0, EXPECTED = 1, REJECTED = 2, INWORK = 3 };
```

Статус сообщения:

* EXPECTED: ожидается (лежит в очереди)
* INWORK: кто-то забрал в работу
* PROCESSED: обработано
* REJECTED: очередь переполнена и не приняла сообщение

```cpp
inline std::string_view ToString(Status status)
```

То же самое, но для статуса.

---

```cpp
struct Message
{
    int id{-1};
    GasType gas_type;
    Status status{EXPECTED};
};
```

Сообщение:

* `id` по умолчанию -1
* `gas_type` без дефолта (будет мусор, если не присвоить)
* `status` по умолчанию EXPECTED

---

```cpp
inline std::ostream &operator<<(std::ostream &out, const Message &mes)
```

Оператор вывода, чтобы можно было писать `std::cout << mes`.

```cpp
    out << "{id: " << mes.id << ", Station: " << ToString(mes.gas_type)
        << ", Status: " << ToString(mes.status) << "}";
```

Печатает красивый вид сообщения.

---

```cpp
struct StoredMessages
{
    int *current_size;
    Message *messages;
};
```

Структура, которая описывает layout shared memory:

* `current_size` указывает на int в общей памяти
* `messages` указывает на массив `Message` в общей памяти

```cpp
#endif
```

Конец include guard.

---

## 4) `queue.hpp / queue.cpp` (очередь в shared memory)

```cpp
#include "messages.hpp"
#include "shared_memory.hpp"
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
```

`Queue` использует `Message`, `SharedMemory`, `std::optional`.

```cpp
constexpr auto protocol_dir = "protocols";
```

Имя папки протоколов. `constexpr auto` тут фактически `const char*`/`const char[]` (в зависимости), используется для путей.

---

### Queue

```cpp
class Queue
{
public:
    Queue(int capacity) : shared_(capacity), capacity_(capacity) {}
```

Конструктор:

* создаёт `SharedMemory shared_(capacity)` (внутри выделяется shared memory под capacity сообщений)
* сохраняет `capacity_`

---

#### Push

```cpp
    Message Push(Message &elem)
```

Кладёт сообщение в очередь.

* Принимает по ссылке, потому что меняет `elem.status` при REJECTED.

```cpp
        auto session = shared_.OpenSession();
```

Открывает сессию доступа к shared memory.

* В конструкторе Session делается `semaphore_->Wait()`, то есть захват семафора.
* Это защита от одновременной записи/чтения.

```cpp
        if (*session.GetMessages()->current_size >= capacity_)
```

Смотрит текущее количество элементов в очереди.

* `GetMessages()` возвращает `StoredMessages*`
* `current_size` это `int*` в shared memory
* `*current_size` это значение размера

```cpp
        {
            elem.status = REJECTED;
            return elem;
        }
```

Если очередь полна:

* помечаем элемент как REJECTED
* возвращаем его

```cpp
        session.GetMessages()->messages[(*session.GetMessages()->current_size)++] = elem;
```

Кладём elem в конец массива и увеличиваем размер.

* Индекс = текущее значение `current_size`
* Потом `current_size++`

```cpp
        return elem;
```

Возвращаем элемент (обычно EXPECTED).

---

#### Pop

```cpp
    std::optional<Message> Pop(const GasType type)
```

Пытается достать первое сообщение нужного типа.

```cpp
        auto session = shared_.OpenSession();
        auto messages = session.GetMessages();
```

Берём доступ к shared memory под семафором.

```cpp
        for (auto i = 0; i < *messages->current_size; ++i)
```

Проходим по всем сообщениям в очереди.

```cpp
            if (messages->messages[i].gas_type == type && messages->messages[i].status == EXPECTED)
```

Ищем сообщение:

* нужного типа
* со статусом EXPECTED (ещё никто не взял)

```cpp
            {
                messages->messages[i].status = INWORK;
```

Помечаем в shared memory, что “в работе”.

```cpp
                const auto mes = messages->messages[i];
```

Копируем сообщение в локальную переменную (уже вне очереди оно будет жить отдельно).

```cpp
                Remove(messages->messages, i, messages->current_size);
```

Удаляем элемент из массива: сдвигаем хвост влево и уменьшаем size.

```cpp
                return mes;
            }
```

Возвращаем `optional` с сообщением.

```cpp
        return {};
```

Если ничего не нашли, возвращаем пустой optional.

---

#### Remove

```cpp
    void Remove(Message *mes, int index, int *size)
```

Удаляет элемент из массива.

```cpp
        for (auto i = index; i < *size; ++i)
        {
            mes[i] = mes[i + 1];
        }
```

Сдвигает элементы после `index` на 1 влево.

**Баг на границе:**
Цикл идёт до `i < *size`, а обращение `mes[i + 1]`.
При `i == *size - 1` ты читаешь `mes[*size]`, это **выход за границы** (UB).
Надо: `i < *size - 1`.

```cpp
        (*size)--;
```

Уменьшаем размер очереди.

---

## 5) `semaphore.hpp` (POSIX семафор-обёртка)

```cpp
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <semaphore.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
```

Подключает POSIX семафоры (`sem_open`, `sem_wait`, ...) и стандартные штуки.

```cpp
class Semaphore
{
public:
    Semaphore(const std::string &name, unsigned int initial_value = 1) : sem_name(name), sem_ptr(nullptr)
```

Конструктор создаёт/открывает именованный семафор.

```cpp
        sem_ptr = sem_open(name.c_str(), O_CREAT, 0777, 1);
```

Создаёт семафор (или открывает, если уже есть).

* `O_CREAT` = создать если нет
* `0777` = права (слишком широкие, но ладно)
* `1` = начальное значение (mutex-подобное)

**Косяк:** ты игноришь `initial_value` и всегда ставишь 1.

```cpp
        if (sem_ptr == SEM_FAILED)
        {
            throw std::runtime_error("Failed to create semaphore: " + name);
        }
```

Если не удалось, кидаешь исключение.

---

```cpp
    Semaphore &operator=(const Semaphore &) = default;
```

Разрешаешь копирующее присваивание.
**Опасно:** копирование этого класса будет копировать указатель `sem_ptr`, и деструкторы двух объектов начнут закрывать/удалять один и тот же семафор.

---

```cpp
    static Semaphore Open(const std::string &name)
```

Открывает уже существующий семафор.

```cpp
        sem_t *sem_ptr = sem_open(name.c_str(), 0);
```

Открытие без `O_CREAT`.

```cpp
        return Semaphore(name, sem_ptr);
```

Возвращает объект, используя приватный конструктор.

---

```cpp
    void Wait()
```

`sem_wait`: уменьшить значение семафора или ждать.

```cpp
    void Post()
```

`sem_post`: увеличить значение семафора (разбудить ожидающих).

```cpp
    void Close()
```

`sem_close`: закрыть дескриптор семафора в процессе.

```cpp
    void Unlink() { sem_unlink(sem_name.c_str()); }
```

`sem_unlink` удаляет имя семафора из системы (как unlink файла).
Существующие открытые дескрипторы могут ещё жить, но новых открыть будет нельзя.

---

```cpp
    ~Semaphore()
    {
        try
        {
            Close();
            Unlink();
        }
```

В деструкторе закрывает и unlink’ает.

**Очень спорно:** unlink делать в каждом процессе почти гарантирует гонки. Если несколько процессов используют один семафор, кто-то его “удалит” раньше времени.

---

## 6) `session.hpp` (RAII-блокировка семафором)

```cpp
class Session
{
public:
    Session(StoredMessages *mes, SemaphorePtr sem) : messages_(mes), semaphore_(std::move(sem)) { semaphore_->Wait(); }
```

Конструктор:

* сохраняет указатель на shared messages
* сохраняет shared_ptr на Semaphore
* сразу делает `Wait()` (захватывает семафор)

То есть создание Session = вход в критическую секцию.

```cpp
    ~Session() { semaphore_->Post(); }
```

Деструктор делает `Post()` (освобождает семафор).
То есть выход из области видимости Session = выход из критической секции.

```cpp
    StoredMessages *GetMessages() { return messages_; }
```

Даёт доступ к данным shared memory.

---

## 7) `settings.hpp` + `GetSettings` (парсер `.conf`)

Тут у тебя много кода, но смысл простой: читать конфиг, резать комментарии, распознавать секции `[create]`, `[station N]`, и ключи `mean`, `stddev`, `type`, `handle.mean`, `handle.stddev`.

Я поясню по блокам, потому что “по каждой строке” на этот кусок будет размером с учебник, но всё равно достаточно подробно.

### Структуры

```cpp
struct Params { double mean{}; double stddev{}; };
```

Параметры нормального распределения: среднее и стандартное отклонение.

```cpp
struct Station { int id{}; GasType type{}; Params handle{}; };
```

Станция:

* id
* тип топлива
* параметры времени обработки

```cpp
struct Settings
{
    std::vector<Station> stations_params{};
    Params create{};
    int number_message_queue{};
};
```

Глобальные настройки:

* список станций
* параметры генерации заявок (create)
* ёмкость очереди

---

### Trim

```cpp
static inline std::string Trim(std::string s)
```

Берёт строку по значению (копию) и возвращает “обрезанную” по краям.

```cpp
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
```

Лямбда: “символ не пробельный”.

```cpp
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
```

Удаляет всё слева, пока не найдёт первый не-пробел.

```cpp
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
```

Удаляет всё справа, пока не найдёт последний не-пробел.

---

### StripInlineComment

```cpp
static inline std::string StripInlineComment(std::string s)
```

Удаляет комментарий в строке.
Комментарий начинается с `#` или `;`.

```cpp
    auto pos1 = s.find('#');
    auto pos2 = s.find(';');
```

Ищет позиции.

```cpp
    auto pos = std::min(pos1 == std::string::npos ? s.size() : pos1,
                        pos2 == std::string::npos ? s.size() : pos2);
```

Берёт минимальную позицию из двух (то есть первый комментарий).

```cpp
    s.resize(pos);
```

Обрезает строку до комментария.

---

### ParseInt / ParseDouble

`ParseInt` использует `std::from_chars`, это быстрый парсер int.

`ParseDouble` использует `std::stod`, потому что `from_chars(double)` не везде норм.

---

### SplitKeyValue

```cpp
auto eq = line.find('=');
```

Ищет `=`.

Если нет, возвращает false.

Разделяет на key и value, trim’ит.

---

### GetSettings

```cpp
inline std::optional<Settings> GetSettings(const std::string& path)
```

Возвращает optional:

* success -> Settings
* fail -> nullopt

```cpp
std::ifstream in(path);
if (!in) return std::nullopt;
```

Открывает файл. Не открылся -> ошибка.

```cpp
Settings settings{};
std::unordered_map<int, Station> stations;
```

Создаёт настройки и мапу станций по id.

```cpp
enum class SectionKind { Root, Create, Station };
SectionKind section = SectionKind::Root;
int current_station_id = -1;
```

Трекер текущей секции конфига.

```cpp
auto require_station = [&](int id) -> Station& { ... };
```

Хелпер: если станции с id нет, создаёт и возвращает ссылку.

---

Цикл чтения:

```cpp
while (std::getline(in, raw))
```

Построчно читает файл.

```cpp
std::string line = Trim(StripInlineComment(raw));
if (line.empty()) continue;
```

Срезает комментарии, тримит, пустые строки пропускает.

---

Секция:

```cpp
if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
```

Если строка вида `[ ... ]`, это заголовок секции.

`[create]` -> `section = Create`
`[station 123]` -> `section = Station`, `current_station_id = 123`

Если секция неизвестна -> `return std::nullopt`.

---

Ключ-значение:

```cpp
if (!SplitKeyValue(line, key, value)) return std::nullopt;
```

Если не распарсилось `key=value`, ошибка.

Дальше в зависимости от `section`:

* Root: разрешён только `number_message_queue`
* Create: разрешены `mean`, `stddev`
* Station: разрешены `type`, `handle.mean`, `handle.stddev`

---

Финализация:

```cpp
settings.stations_params.clear();
settings.stations_params.reserve(stations.size());
for (auto& kv : stations) settings.stations_params.push_back(std::move(kv.second));
std::sort(... by id ...);
```

Переносит станции из map в vector и сортирует.

Валидация:

* `number_message_queue > 0`
* станции не пустые

---

## 8) `shared_memory.hpp` (System V shm + shmat + semaphore)

```cpp
class SharedMemory
{
public:
    SharedMemory(int size) : semaphore_(std::make_shared<Semaphore>(SEMAPHORE_NAME))
```

Конструктор:

* создаёт именованный семафор (имя SEMAPHORE_NAME должно быть где-то определено)
* shared_ptr, чтобы сессии его разделяли

```cpp
        std::ofstream{SHARED_MEMORY_NAME};
```

Создаёт файл `SHARED_MEMORY_NAME` (пустой), чтобы `ftok` мог от него сделать ключ.

```cpp
        const auto key = ftok(SHARED_MEMORY_NAME, 652);
```

`ftok` генерирует System V IPC key на основе пути и proj_id (652).

Если -1 -> ошибка.

```cpp
        shm_fd_ = shmget(key, sizeof(Message) * size + sizeof(int) + 1, 0666 | IPC_CREAT);
```

Создаёт/открывает сегмент shared memory.
Размер:

* `sizeof(int)` под `current_size`
* `sizeof(Message) * size` под массив сообщений
* `+ 1` (это странно и бессмысленно, один байт погоды не делает, но ладно)

Права `0666` и `IPC_CREAT`.

```cpp
        auto memory = shmat(shm_fd_, nullptr, 0);
```

Подключает shared memory к адресному пространству процесса.
Возвращает указатель.

**Ошибка сравнения:** `shmat` возвращает `(void*)-1` при ошибке, а не `MAP_FAILED` (это для mmap). У тебя проверка:

```cpp
if (memory == MAP_FAILED)
```

Это неправильно. Надо:

```cpp
if (memory == (void*)-1)
```

---

```cpp
        int *size_queue = reinterpret_cast<int *>(memory);
        *size_queue = 0;
```

В начале сегмента кладёшь `current_size = 0`.

```cpp
        Message *messages = reinterpret_cast<Message *>(size_queue + 1);
```

Сразу после int начинается массив Message.

```cpp
        messages_ = StoredMessages{size_queue, messages};
```

Сохраняешь указатели.

---

Деструктор:

```cpp
    ~SharedMemory()
    {
        std::filesystem::remove(SHARED_MEMORY_NAME);
        shmdt(messages_.current_size);
        shmctl(shm_fd_, IPC_RMID, nullptr);
    }
```

* удаляет файл-метку
* отключает память `shmdt`
* удаляет сегмент `shmctl(... IPC_RMID ...)`

**Если несколько процессов используют shared memory, удаление в деструкторе каждого процесса очень легко ломает жизнь остальным.** Обычно удаляет тот, кто “создатель/владелец”, и только один раз.

---

```cpp
    Session OpenSession() { return Session(&messages_, semaphore_); }
```

Возвращает Session, которая:

* ждёт семафор в конструкторе
* освобождает в деструкторе

---

## Самые важные “чтобы обезьяна не докопалась” проблемы в этом коде

1. **В main**: `if (argc > 0)` должно быть `if (argc > 1)`, иначе `argv[1]` = UB.
2. **condition_variable**: ждать без флага/предиката неправильно (spurious wakeup + потеря события).
3. **signal handler**: `std::cout` и `notify.notify_all()` из обработчика сигнала потенциально опасны.
4. **Remove в Queue**: выход за границы массива (`mes[i+1]` при последнем i).
5. **shmat проверка**: сравнение с `MAP_FAILED` неверно.
6. **Завершение процессов**: деструктор родителя ждёт детей, но не будит/не убивает их. Они могут навечно висеть в `sigwait`.
7. **Нормальное распределение**: может дать отрицательное время, sleep “в минус” логически бред.
8. **Semaphore деструктор**: `Unlink` в каждом процессе может сломать другим процессам открытие/работу.

---

Если тебе нужно, я могу **в следующем сообщении** (без вопросов и “уточните”) просто взять и:

* переписать `main + MainLoop` так, чтобы корректно завершалось по Ctrl+C,
* убрать опасные вызовы из signal handler,
* сделать нормальный `stop_flag`,
* исправить `Remove`,
* починить `shmat`,
* и сделать “мягкое” завершение детей (SIGTERM/SIGUSR1 + проверка run_).
