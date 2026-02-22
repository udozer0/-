#pragma once
#include "messages.hpp"
#include "shared_memory.hpp"
#include <optional>

class Queue {
public:
    explicit Queue(int capacity);

    bool Push(Message msg);                          // Producer
    std::optional<Message> Pop(GasType type);        // Consumer

private:
    void P(int sem_num) { semop(semid_, &sops[sem_num], 1); }
    void V(int sem_num) { sops[sem_num].sem_op = 1; P(sem_num); }

    SharedMemory shm_;
    int semid_;
    int capacity_;
    struct sembuf sops[NUM_SEMS]{};
};