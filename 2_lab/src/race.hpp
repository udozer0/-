#include "message-queue.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <mutex>
#include <sstream>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <utility>
#include <vector>

using CarsPosition = std::vector<double>;

using PartResults = std::vector<std::vector<std::pair<int, int>>>;

class Race
{
public:
    Race() : positions_(6, 0), results_(4, std::vector<std::pair<int, int>>(5, {-1, 0})) {}

    ~Race() { thread_.join(); }

    CarsPosition GetPositions()
    {
        std::lock_guard lock(mtx_);
        return positions_;
    }

    void StartRace()
    {
        thread_ = std::thread{[this]()
                              {
                                  for (number_part_ = 0; number_part_ < 3; ++number_part_)
                                  {
                                      // SIGUSR1 - посылаем сигнал о начале очередного заезда
                                      killpg(getpid(), SIGUSR1);
                                      UpdatePositions(number_part_);

                                      // Делаем задержку между этапами
                                      std::this_thread::sleep_for(std::chrono::seconds(1));
                                  }

                                  CalculateFinallyResult();
                              }};
    }

    std::vector<std::pair<int, int>> GetResult()
    {
        std::lock_guard lock(mtx_);
        return results_[number_part_];
    }

    auto GetAllResults()
    {
        std::lock_guard lock(mtx_);
        return results_;
    }

    bool IsFinally() { return number_part_ == 3; }

    void UpdatePositions(const int part)
    {
        int barrier = 0;
        int place = 1;

        const auto time_start = std::chrono::steady_clock::now();

        while (barrier != 5)
        {
            std::stringstream updating_car;
            updating_car << queue_.receive();

            int car_id;
            updating_car >> car_id;

            double position;
            updating_car >> position;

            int is_finally;
            updating_car >> is_finally;

            std::lock_guard lock(mtx_);

            if (is_finally)
            {
                barrier++;

                const auto time_end = std::chrono::steady_clock::now();

                const auto result = std::chrono::duration_cast<std::chrono::milliseconds>(time_end - time_start);

                results_[part][car_id].first = place++;
                results_[part][car_id].second = result.count();
            }

            positions_[car_id] = position;
        }
    }

    void CalculateFinallyResult()
    {
        std::vector<std::pair<int, int>> cars_score = {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}};
        for (int i = 0; i < 5; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                cars_score[i].first += results_[j][i].second;
            }
        }

        std::sort(cars_score.begin(), cars_score.end());

        for (int i = 0; i < 5; ++i)
        {
            results_[number_part_][cars_score[i].second] = std::pair{i + 1, cars_score[i].first};
        }
    }

private:
    MessageQueue queue_{MQ_NAME, true};
    CarsPosition positions_;

    std::mutex mtx_;

    std::atomic<int> number_part_;

    PartResults results_;
    std::thread thread_;
};
