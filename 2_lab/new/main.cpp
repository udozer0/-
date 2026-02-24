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
constexpr int number_of_stages = 3;
constexpr int cars_number = 5;
constexpr int finish_line = 100;
constexpr int track_length = 50;

std::atomic_bool start_flag {};
std::atomic_bool next_flag {};
}  

// ------------------
// Простая печать
// ------------------
template <typename... Args>
void Print(Args&&... args)
{
  (std::cout << ... << std::forward<Args>(args));
  std::cout.flush();
}

template <typename... Args>
void Println(Args&&... args)
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

    for (int stage = 1; stage <= number_of_stages; ++stage)
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

      ProgressMessage msg {};
      msg.id = id;
      msg.progress = progress;
      msg.finished = 1;

      msgsnd(queue, &msg, sizeof(msg) - sizeof(long), 0);

      if (stage == number_of_stages) continue;

      while (!next_flag) pause();
      next_flag = false;
    }
  }

  int progress {};
  int order {};
  int points {};
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
    for (int stage = 1; stage <= number_of_stages; ++stage)
    {
      current_stage = stage;
      finish_order_counter = 0;

      for (auto& car : cars)
      {
        car.progress = 0;
        car.order = 0;
        car.finished = false;
      }

      Println("Подготовка этапа ", stage);
      sleep(1);

      kill(-process_group, SIGUSR1);

      unsigned finished_count = 0;

      while (finished_count < cars_number)
      {
        ProgressMessage msg {};

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
            car.points += car.order;
          }
        }

        finished_count = 0;
        for (const auto& car : cars)
          if (car.finished) ++finished_count;

        display_progress();
        usleep(200000);
      }

      display_points();

      if (stage == number_of_stages)
      {
        Println("Гонка завершена");
        continue;
      }

      Println("\nНажмите Enter для начала следующего этапа...");
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

      kill(-process_group, SIGUSR2);
    }

    display_results();

    for (const auto& pid : processes)
      waitpid(pid, nullptr, 0);
  }

 private:
  void display_progress() const
  {
    Print("\033[2J\033[1;1H");
    Println("Прогресс этапа ", current_stage, ":");

    for (int i = 0; i < cars_number; ++i)
    {
      const Car& car = cars[i];

      std::string bar(track_length, '.');
      int pos = (car.progress * track_length) / finish_line;
      pos = std::clamp(pos, 0, track_length);

      for (int j = 0; j < pos && j < track_length; ++j)
        bar[j] = '=';

      if (pos < track_length)
        bar[pos] = '>';

      Print("Машина ", i + 1,
            " : [", bar, "] ",
            car.progress, " / ", finish_line);

      if (car.finished)
        Println(" (Финишировала с местом: ", car.order, ")");
      else
        Println();
    }
  }

  void display_points() const
  {
    Println("\nРезультаты этапа:");

    std::array<const Car*, cars_number> order_ptrs {};

    for (int i = 0; i < cars_number; ++i)
      order_ptrs[i] = &cars[i];

    std::sort(order_ptrs.begin(), order_ptrs.end(),
              [](const Car* a, const Car* b)
              {
                return a->order < b->order;
              });

    for (int i = 0; i < cars_number; ++i)
    {
      const Car* car = order_ptrs[i];
      int index = car - &cars[0];

      Println("Место ", i + 1,
              ": Машина ", index + 1,
              " (Очки: ", car->points, ")");
    }
  }

  void display_results() const
  {
    Println("\nИтоговые результаты:");

    std::array<const Car*, cars_number> score_ptrs {};

    for (int i = 0; i < cars_number; ++i)
      score_ptrs[i] = &cars[i];

    std::sort(score_ptrs.begin(), score_ptrs.end(),
              [](const Car* a, const Car* b)
              {
                return a->points < b->points;
              });

    for (int i = 0; i < cars_number; ++i)
    {
      const Car* car = score_ptrs[i];
      int index = car - &cars[0];

      Println("Место ", i + 1,
              ": Машина ", index + 1,
              " (Всего очков: ", car->points, ")");
    }
  }

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
}