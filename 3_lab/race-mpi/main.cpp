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
#include <numeric>

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

static std::string Pad2(int x)
{
    if (x < 10) return MakeStr("0", x);
    return MakeStr(x);
}

// если захочешь: 123456 ms -> 02:03.456
static std::string FormatMs(int ms)
{
    int m = ms / 60000;
    int s = (ms / 1000) % 60;
    int r = ms % 1000;
    return MakeStr(Pad2(m), ":", Pad2(s), ".", std::string(3 - std::to_string(r).size(), '0'), r, " ms");
}

// вычисляем места по текущей позиции (больше позиция -> лучше место)
static std::vector<int> ComputePlaces(const std::vector<int>& pos)
{
    int n = (int)pos.size();
    std::vector<int> idx(n);
    std::iota(idx.begin(), idx.end(), 0);

    // сортируем только по позиции, root (0) тоже попадет, но мы его не печатаем
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return pos[a] > pos[b];
    });

    std::vector<int> place(n, 0);
    int cur_place = 1;
    for (int id : idx)
        place[id] = cur_place++;

    return place;
}

static void RenderFrame(int stage, const std::vector<int>& pos)
{
    std::cout << "\033[2J\033[H";
    std::cout << MakeStr("Этап ", stage, "/", number_parts, "\n\n");

    auto place = ComputePlaces(pos);

    for (int r = 1; r < (int)pos.size(); ++r)
    {
        int p = std::clamp(pos[r], 0, track_len);
        int progress = (int)((long long)p * 100 / track_len);

        std::string line;
        line.reserve(track_len + 2);
        line.append(p, '-');
        line.push_back('>');
        line.append(track_len - p, ' ');

        std::cout << MakeStr(
            "car ", r, " |", line, "| ",
            progress, " / 100  (place: ", place[r], ")\n"
        );
    }
    std::cout.flush();
}

// Возвращает отсортированный по времени список (car, time_ms)
static std::vector<std::pair<int,int>> BuildStageResults(const std::vector<int>& finish_times, int size)
{
    std::vector<std::pair<int,int>> stage_res;
    stage_res.reserve(std::max(0, size - 1));

    for (int car = 1; car < size; ++car)
        stage_res.push_back({car, finish_times[car]});

    std::sort(stage_res.begin(), stage_res.end(),
              [](const auto& a, const auto& b) { return a.second < b.second; });

    return stage_res;
}

static void PrintStageResults(int stage, const std::vector<std::pair<int,int>>& stage_res)
{
    std::cout << "\nРезультаты этапа " << stage << ":\n";
    int place = 1;
    for (const auto& [car, t] : stage_res)
    {
        std::cout << MakeStr("Место ", place++, ": Машина ", car, " (время: ", t, " ms)\n");
        // если хочешь более “временной” вид:
        // std::cout << MakeStr("Место ", place++, ": Машина ", car, " (время: ", FormatMs(t), ")\n");
    }
}

static void PrintAllStagesSummary(const std::vector<std::vector<std::pair<int,int>>>& all_stages)
{
    std::cout << "\n=== Результаты этапов (сводка) ===\n";
    for (int stage = 1; stage <= (int)all_stages.size(); ++stage)
    {
        std::cout << "\nРезультаты этапа " << stage << ":\n";
        int place = 1;
        for (const auto& [car, t] : all_stages[stage - 1])
        {
            std::cout << MakeStr("Место ", place++, ": Машина ", car, " (время: ", t, " ms)\n");
        }
    }
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
        std::map<int, int> total_time; // car -> sum ms

        // НОВОЕ: копим результаты каждого этапа, чтобы вывести в конце
        std::vector<std::vector<std::pair<int,int>>> all_stage_results;
        all_stage_results.reserve(number_parts);

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

            // Строим результаты этапа (отсортированные)
            auto stage_res = BuildStageResults(finish_times, size);

            // Оставляем вывод результатов этапа сразу после этапа
            PrintStageResults(stage, stage_res);

            // НОВОЕ: сохраняем результаты этапа для вывода в конце
            all_stage_results.push_back(stage_res);

            // Обновляем суммарное время
            for (int car = 1; car < size; ++car)
                total_time[car] += finish_times[car];

            std::cout << "\n----------------------------------------\n";
            std::cout << "Жми Enter для следующего этапа...\n";
            std::cout.flush();
            std::cin.get();
        }

        // НОВОЕ: печатаем сводку по этапам в конце (для сравнения)
        PrintAllStagesSummary(all_stage_results);

        std::vector<std::pair<int, int>> result(total_time.begin(), total_time.end());
        std::sort(result.begin(), result.end(),
                  [](auto& l, auto& r) { return l.second < r.second; });

        std::cout << "\n=== Итоги гонки ===\n";
        int place = 1;
        for (const auto& [car, time] : result)
        {
            std::cout << MakeStr("Место ", place++, ": Машина ", car,
                                 " (общее время: ", time, " ms)\n");
            // или красивее:
            // std::cout << MakeStr("Место ", place++, ": Машина ", car,
            //                      " (общее время: ", FormatMs(time), ")\n");
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

            if (finish_ms < 0)
            {
                finish_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - started).count();
            }

            MPI_Gather(&finish_ms, 1, MPI_INT, nullptr, 0, MPI_INT, 0, MPI_COMM_WORLD);

            MPI_Barrier(cars);
        }
    }

    MPI_Comm_free(&cars);
    MPI_Finalize();
    return 0;
}