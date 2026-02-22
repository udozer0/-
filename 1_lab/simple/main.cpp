#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <random>
#include <ranges>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

#include "nlohmann/json.hpp"

using time_point = std::chrono::system_clock::time_point;

template <typename T>
class SharedMemory
{
 public:
  ~SharedMemory()
  {
    shmdt(object);
    shmctl(identity, IPC_RMID, nullptr);
  }

  [[nodiscard]] T* operator->() const { return object; }
  [[nodiscard]] T& operator*() const { return *object; }

 private:
  int identity { shmget(IPC_PRIVATE, sizeof(T), IPC_CREAT | 0666) };
  T* object { new (shmat(identity, nullptr, 0)) T {} };
};

enum class Fuel
{
  AI76,
  AI92,
  AI95,
  COUNT,
};

inline static void from_json(const nlohmann::json& j, Fuel& fuel)
{
  auto str = j.get<std::string>();
  if (str == "АИ76")
    fuel = Fuel::AI76;
  else if (str == "АИ92")
    fuel = Fuel::AI92;
  else if (str == "АИ95")
    fuel = Fuel::AI95;
  else
    throw std::runtime_error("Unknown fuel type: " + str);
}

inline const char* to_string(Fuel fuel)
{
  switch (fuel)
  {
    case Fuel::AI76: return "АИ76";
    case Fuel::AI92: return "АИ92";
    case Fuel::AI95: return "АИ95";
    default: return "Unknown";
  }
}

inline std::string format_time(const time_point& tp)
{
  std::time_t tt = std::chrono::system_clock::to_time_t(tp);
  std::tm tm = *std::localtime(&tt);

  char buffer[64];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);

  return std::string(buffer);
}

struct Column
{
  void serve(int) const;

  Fuel fuel {};
  double mean_service_time {};
  double standard_deviation {};

  friend void from_json(const nlohmann::json& j, Column& t)
  {
    const Column def {};
    t.fuel = j.value("fuel", def.fuel);
    t.mean_service_time = j.value("mean_service_time", def.mean_service_time);
    t.standard_deviation = j.value("standard_deviation", def.standard_deviation);
  }
};

struct Car
{
  int id {};
  Fuel fuel {};
  time_point timestamp {};
};

struct Queue
{
  explicit Queue()
  {
    sem_init(&mutex, 1, 1);

    std::ranges::for_each(fuel_semaphores,
                          [](auto& semaphore) { sem_init(&semaphore, 1, 0); });
  };

  ~Queue()
  {
    sem_destroy(&mutex);

    std::ranges::for_each(fuel_semaphores,
                          [](auto& semaphore) { sem_destroy(&semaphore); });

    if (inserted) std::fclose(inserted);
    if (dropped) std::fclose(dropped);
  }

  void lock() { sem_wait(&mutex); }
  void unlock() { sem_post(&mutex); }

  [[nodiscard]] std::optional<Car> find_nearest_car(const Fuel& f)
  {
    std::optional<Car> result {};

    lock();

    auto it = std::find_if(cars.begin(), cars.begin() + current_size,
                           [&](const auto& c) { return c.fuel == f; });

    if (it != cars.begin() + current_size)
    {
      result = *it;
      std::move(it + 1, cars.begin() + current_size, it);
      current_size--;
    }

    unlock();
    return result;
  }

  void insert_car(const Car& car)
  {
    lock();

    std::ostringstream oss;

    if (current_size < max_size)
    {
      cars[current_size++] = car;

      oss << "Машина попала в очередь: номер - "
          << car.id
          << ", тип топлива - "
          << to_string(car.fuel)
          << ", временная метка - "
          << format_time(car.timestamp);

      const std::string str = oss.str();

      std::cout << str << std::endl;
      std::fprintf(inserted, "%s\n", str.c_str());
      std::fflush(inserted);
    }
    else
    {
      oss << "Машина не попала в очередь: номер - "
          << car.id
          << ", тип топлива - "
          << to_string(car.fuel)
          << ", временная метка - "
          << format_time(car.timestamp);

      const std::string str = oss.str();

      std::cout << str << std::endl;
      std::fprintf(dropped, "%s\n", str.c_str());
      std::fflush(dropped);
    }

    unlock();
  }

  static constexpr auto max_size { 15 };

  std::array<Car, max_size> cars {};
  std::size_t current_size {};

  std::array<sem_t, static_cast<std::size_t>(Fuel::COUNT)> fuel_semaphores {};
  sem_t mutex {};

  bool finished { false };

  std::FILE* inserted { std::fopen("inserted.log", "w") };
  std::FILE* dropped { std::fopen("dropped.log", "w") };
};

struct Generator
{
  static void generate();

  static inline int requests { 150 };
  static inline double mean_generation_time { 1.0 };
  static inline double standard_deviation { 0.5 };
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Generator, requests, mean_generation_time, standard_deviation);

auto const queue { SharedMemory<Queue> {} };

std::array<Column, 5> columns {
  Column { .fuel = Fuel::AI76, .mean_service_time = 10,   .standard_deviation = 0.5 },
  Column { .fuel = Fuel::AI76, .mean_service_time = 10,   .standard_deviation = 0.5 },
  Column { .fuel = Fuel::AI92, .mean_service_time = 12.5, .standard_deviation = 0.6 },
  Column { .fuel = Fuel::AI92, .mean_service_time = 12.5, .standard_deviation = 0.6 },
  Column { .fuel = Fuel::AI95, .mean_service_time = 15,   .standard_deviation = 0.7 },
};

void Generator::generate()
{
  std::normal_distribution<> dist(mean_generation_time, standard_deviation);
  std::mt19937 rng(std::random_device {}());

  for (int request = 0; request < requests; ++request)
  {
    std::this_thread::sleep_for(std::chrono::duration<double>(dist(rng)));

    const auto fuel = std::invoke([&] {
      std::uniform_int_distribution<> d(0, 4);
      const auto v = d(rng);
      return v < 2 ? Fuel::AI76 : v < 4 ? Fuel::AI92 : Fuel::AI95;
    });

    queue->insert_car({
        .id = request,
        .fuel = fuel,
        .timestamp = std::chrono::system_clock::now(),
    });

    sem_post(&queue->fuel_semaphores[static_cast<std::size_t>(fuel)]);
  }

  queue->finished = true;
}

static void log_service_start(std::FILE* log, int index, const Car& car)
{
  std::ostringstream oss;
  oss << "Колонка "
      << index
      << " начала обслуживание машины с номером "
      << car.id
      << " и типом топлива "
      << to_string(car.fuel)
      << " во временной метке "
      << format_time(car.timestamp);

  const std::string str = oss.str();

  std::cout << str << std::endl;
  std::fprintf(log, "%s\n", str.c_str());
  std::fflush(log);
}

void Column::serve(int index) const
{
  std::normal_distribution<> dist(mean_service_time, standard_deviation);
  std::mt19937 rng(std::random_device {}());

  char filename[64];
  std::snprintf(filename, sizeof(filename), "column_%d.log", index);
  const auto log = std::fopen(filename, "w");

  if (!log)
  {
    std::perror("fopen column log failed");
    return;
  }

  while (!queue->finished)
  {
    sem_wait(&queue->fuel_semaphores[static_cast<std::size_t>(fuel)]);

    const auto car = queue->find_nearest_car(fuel);
    if (!car) continue;

    log_service_start(log, index, *car);

    std::this_thread::sleep_for(std::chrono::duration<double>(dist(rng)));
  }

  // Додавливаем хвост очереди по своему топливу
  while (true)
  {
    queue->lock();
    bool has_car = std::any_of(queue->cars.begin(),
                              queue->cars.begin() + queue->current_size,
                              [&](const Car& c) { return c.fuel == fuel; });
    queue->unlock();

    if (!has_car)
      break;

    auto car = queue->find_nearest_car(fuel);
    if (car)
    {
      log_service_start(log, index, *car);
      std::this_thread::sleep_for(std::chrono::duration<double>(dist(rng)));
    }
    else
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  }

  std::fclose(log);
}

int main(int argc, char** argv)
{
  if (argc < 2)
  {
    std::cerr << "Usage: " << argv[0] << " <config.json>\n";
    return 1;
  }

  nlohmann::json configuration {};
  std::ifstream { argv[1] } >> configuration;

  columns = configuration["columns"].get<std::array<Column, 5>>();
  configuration["generator"].get<Generator>(); // наполняет static поля

  std::vector<pid_t> pids;
  pids.reserve(columns.size());

  int index = 0;
  for (const auto& column : columns)
  {
    ++index;

    pid_t pid = fork();
    if (pid < 0)
    {
      std::perror("fork failed");
      return 2;
    }

    if (pid == 0)
    {
      column.serve(index);
      return 0;
    }

    pids.push_back(pid);
  }

  Generator::generate();

  // Разбудим колонки, чтобы они могли выйти/дожать хвост
  for (const auto& column : columns)
    sem_post(&queue->fuel_semaphores[static_cast<std::size_t>(column.fuel)]);

  for (auto pid : pids)
    waitpid(pid, nullptr, 0);

  return 0;
}