

#include "message-queue.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <random>
#include <string>
#include <thread>

class Car
{
public:
    Car(int car_name, const double distance = 0) : car_id_(car_name), distance_(distance)
    {
        thread_ = std::thread{&Car::Race, this};
    }

    void Start()
    {
        speed_ = GetRandom<double>(0.003, 0.0008);
        distance_ += 0.333;
        notify_.notify_one();
    }

    void Wait() { thread_.join(); };

private:
    void Race()
    {
        double position;

        while (position < 0.9)
        {

            std::unique_lock lock(cond_lock_);
            notify_.wait(lock);

            while (position < distance_)
            {
                position += speed_;

                queue_.send(std::to_string(car_id_) + " " + std::to_string(position) + " 0");

                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }

            queue_.send(std::to_string(car_id_) + " " + std::to_string(position) + " 1");
        }
    }

    template <class T> T GetRandom(double mean, double stddev = 3)
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::normal_distribution<double> dist(mean, stddev);

        return dist(gen);
    }

private:
    std::thread riding_;

    double speed_;

    int car_id_;
    double distance_{1};

    std::thread thread_;

    MessageQueue queue_{MQ_NAME};

    std::condition_variable notify_;
    std::mutex cond_lock_;
};