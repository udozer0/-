#include <chrono>
#include <csignal>
#include <cstdlib>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "car.hpp"
#include "race.hpp"

using namespace ftxui;

std::unique_ptr<Car> car = nullptr;

void signalHandler(int signum)
{
    if (car != nullptr)
        car->Start();
}

int main()
{
    signal(SIGUSR1, signalHandler);

    const auto parent_pid = getpid();

    for (auto i = 0; i < 5; ++i)
    {
        const auto pid = fork();

        if (pid == 0)
        {
            car = std::make_unique<Car>(i);

            setpgid(0, parent_pid);
            car->Wait();

            return 0;
        }
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));

    Race race;
    race.StartRace();

    auto screen = ScreenInteractive::Fullscreen();

    std::vector<std::string> headers = {"Машина 1", "Машина 2", "Машина 3", "Машина 4", "Машина 5"};

    std::vector<std::vector<int>> all_results{};

    auto renderTable = [&]
    {
        const auto results = race.GetAllResults();

        Elements rows;
        // Заголовки
        Elements header_row;
        header_row.push_back(text("Этап") | border | center | size(WIDTH, EQUAL, 20));
        for (const auto &header : headers)
        {
            header_row.push_back(text(header) | border | center | size(WIDTH, EQUAL, 20));
        }
        rows.push_back(hbox(std::move(header_row)));

        // Данные таблицы
        for (size_t i = 0; i < results.size(); ++i)
        {
            Elements row;

            if (std::find_if(results[i].begin(), results[i].end(),
                             [](const auto result) { return result.first == -1; }) != results[i].end())
                continue;

            row.push_back(text((i != 3 ? "Этап " + std::to_string(i + 1) : "Итог ")) | border | center |
                          size(WIDTH, EQUAL, 20));

            for (size_t j = 0; j < results[i].size(); ++j)
            {

                row.push_back(text((std::to_string(results[i][j].first) != "-1"
                                        ? std::to_string(results[i][j].first) +
                                              " место: " + std::to_string(results[i][j].second) + "мс"
                                        : "")) |
                              border | center | size(WIDTH, EQUAL, 20));
            }
            rows.push_back(hbox(std::move(row)));
        }

        return vbox(rows);
    };

    auto component = Renderer(
        [&]
        {
            const auto results = race.GetResult();

            const auto positions = race.GetPositions();

            Color colorPlace = race.IsFinally() ? Color::Blue : Color::Green;

            return vbox({hbox({text("Машина 1: ") | color(Color::Blue) | bold | center,
                               gauge(positions[0]) | color(Color::Red) | border | size(WIDTH, EQUAL, 60),
                               (results[0].first != -1 ? text(std::to_string(results[0].first) + " место") : text("")) |
                                   color(colorPlace) | bold | center}),

                         hbox({text("Машина 2: ") | color(Color::Blue) | bold | center,
                               gauge(positions[1]) | color(Color::Red) | border | size(WIDTH, EQUAL, 60),
                               (results[1].first != -1 ? text(std::to_string(results[1].first) + " место") : text("")) |
                                   color(colorPlace) | bold | center}),

                         hbox({
                             text("Машина 3: ") | color(Color::Blue) | bold | center,
                             gauge(positions[2]) | color(Color::Red) | border | size(WIDTH, EQUAL, 60),
                             (results[2].first != -1 ? text(std::to_string(results[2].first) + " место") : text("")) |
                                 color(colorPlace) | bold | center,
                         }),

                         hbox({
                             text("Машина 4: ") | color(Color::Blue) | bold | center,
                             gauge(positions[3]) | color(Color::Red) | border | size(WIDTH, EQUAL, 60),
                             (results[3].first != -1 ? text(std::to_string(results[3].first) + " место") : text("")) |
                                 color(colorPlace) | bold | center,
                         }),

                         hbox({text("Машина 5: ") | color(Color::Blue) | bold | center,
                               gauge(positions[4]) | color(Color::Red) | border | size(WIDTH, EQUAL, 60),
                               (results[4].first != -1 ? text(std::to_string(results[4].first) + " место") : text("")) |
                                   color(colorPlace) | bold | center}),
                         renderTable()});
        });

    std::thread(
        [&]
        {
            while (true)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                screen.PostEvent(Event::Custom);
            }
        })
        .detach();

    screen.Loop(component);
}
