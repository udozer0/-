#include <chrono>
#include <iostream>
#include <map>
#include <random>
#include <ranges>
#include <thread>
#include <vector>
#include <sstream>
#include <algorithm>
#include <string>

#include "mpi.h"

using namespace std::ranges;

template <typename... Args>
std::string MakeStr(Args&&... args)
{
    std::ostringstream oss;
    (oss << ... << std::forward<Args>(args));
    return oss.str();
}

constexpr int number_parts = 3;
constexpr int track_len = 50;
constexpr int max_ticks = 600;

static void RenderFrame(int stage, const std::vector<int>& pos)
{
    std::cout << "\033[2J\033[H";
    std::cout << MakeStr("Этап ", stage, "/", number_parts, "\n\n");

    for (int r = 1; r < (int)pos.size(); ++r)
    {
        int p = std::clamp(pos[r], 0, track_len);

        std::string line;
        line.reserve(track_len + 2);
        line.append(p, '-');
        line.push_back('>');
        line.append(track_len - p, ' ');

        std::cout << MakeStr("car ", r, " |", line, "|\n");
    }
    std::cout.flush();
}

int main(int argc, char** argv)
{
    MPI_Init(&argc, &argv);

    int rank{};
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    int size{};
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    MPI_Comm cars{};
    MPI_Comm_split(MPI_COMM_WORLD, rank != 0, rank, &cars);

    std::vector<int> positions(size, 0);
    std::vector<int> finish_times(size, 0); // ms до финиша (для судьи)

    if (rank == 0)
    {
        std::map<int, int> total_time;

        for (auto stage : views::iota(1, number_parts + 1))
        {
            int start_signal = 1;
            MPI_Bcast(&start_signal, 1, MPI_INT, 0, MPI_COMM_WORLD);

            std::fill(positions.begin(), positions.end(), 0);
            RenderFrame(stage, positions);

            int stop_flag = 0;

            for (int tick = 0; tick < max_ticks; ++tick)
            {
                int root_dummy_pos = 0;
                MPI_Gather(&root_dummy_pos, 1, MPI_INT,
                           positions.data(), 1, MPI_INT,
                           0, MPI_COMM_WORLD);

                RenderFrame(stage, positions);
                std::this_thread::sleep_for(std::chrono::milliseconds(80));

                bool all_finished = true;
                for (int r = 1; r < size; ++r)
                    all_finished &= (positions[r] >= track_len);

                stop_flag = all_finished ? 1 : 0;
                MPI_Bcast(&stop_flag, 1, MPI_INT, 0, MPI_COMM_WORLD);

                if (stop_flag) break;
            }

            // Собираем "время до финиша" каждой машины
            int root_time = 0;
            MPI_Gather(&root_time, 1, MPI_INT,
                       finish_times.data(), 1, MPI_INT,
                       0, MPI_COMM_WORLD);

            std::cout << "\n";
            for (int car = 1; car < size; ++car)
            {
                std::cout << MakeStr("Машина id(", car, ") закончила этап ", stage,
                                     " за ", finish_times[car], " ms\n");
                total_time[car] += finish_times[car];
            }

            std::cout << "\n----------------------------------------\n";
            std::cout << "Жми Enter для следующего этапа...\n";
            std::cout.flush();
            std::cin.get();
        }

        std::vector<std::pair<int, int>> result(total_time.begin(), total_time.end());
        std::sort(result.begin(), result.end(),
                  [](auto& l, auto& r) { return l.second < r.second; });

        std::cout << "\n=== Итоги ===\n";
        int place = 1;
        for (const auto& [car, time] : result)
        {
            std::cout << MakeStr("Машина id(", car, ") заняла место ", place++,
                                 ", суммарно потратила ", time, " ms\n");
        }
    }
    else
    {
        std::mt19937 gen(
            (unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count()
            ^ (rank * 0x9e3779b9u)
        );

        std::uniform_int_distribution<int> step_dist(0, 3);       // рывки
        std::uniform_int_distribution<int> sleep_dist(60, 220);   // “явная” разница скоростей

        for (auto stage : views::iota(1, number_parts + 1))
        {
            int start_signal{};
            MPI_Bcast(&start_signal, 1, MPI_INT, 0, MPI_COMM_WORLD);

            int pos = 0;
            int stop_flag = 0;

            auto started = std::chrono::steady_clock::now();

            // Вот это важно: фиксируем момент финиша, но не выходим из цикла
            int finish_ms = -1;

            for (int tick = 0; tick < max_ticks; ++tick)
            {
                if (pos < track_len)
                {
                    pos = std::min(track_len, pos + step_dist(gen));

                    if (pos >= track_len && finish_ms < 0)
                    {
                        finish_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - started).count();
                    }
                }

                MPI_Gather(&pos, 1, MPI_INT, nullptr, 0, MPI_INT, 0, MPI_COMM_WORLD);

                MPI_Bcast(&stop_flag, 1, MPI_INT, 0, MPI_COMM_WORLD);
                if (stop_flag) break;

                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_dist(gen)));
            }

            // Если вдруг кто-то не успел (маловероятно), считаем по факту конца
            if (finish_ms < 0)
            {
                finish_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started).count();
            }

            // Отправляем СВОЁ время финиша, а не общее время этапа
            MPI_Gather(&finish_ms, 1, MPI_INT, nullptr, 0, MPI_INT, 0, MPI_COMM_WORLD);

            MPI_Barrier(cars);
        }
    }

    MPI_Comm_free(&cars);
    MPI_Finalize();
    return 0;
}
