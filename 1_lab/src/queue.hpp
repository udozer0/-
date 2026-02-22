#pragma once
#include "messages.hpp"
#include "shared_memory.hpp"
#include <optional>

class Queue {
public:
    explicit Queue(int capacity);

    bool Push(Message msg);
    std::optional<Message> Pop(GasType type);

private:
    void P(int sem_num);
    void V(int sem_num);

    SharedMemory shm_;
    int semid_;
    int capacity_;
    struct sembuf sops[5]{};
};