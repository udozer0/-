#pragma once
#include "messages.hpp"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <filesystem>

constexpr char KEY_FILE[] = "protocols/queue";   // для ftok
constexpr int NUM_SEMS = 5;

#define SEM_MUTEX 0
#define SEM_EMPTY 1
#define SEM_76    2
#define SEM_92    3
#define SEM_95    4

struct SharedQueue {
    int size;
    Message messages[1];   // flexible array (выделяем с запасом)
};

class SharedMemory {
public:
    SharedMemory(int capacity);
    ~SharedMemory();

    int semid() const { return semid_; }
    int shmid() const { return shmid_; }
    SharedQueue* attach() { return static_cast<SharedQueue*>(shmat(shmid_, nullptr, 0)); }

private:
    key_t key_ = -1;
    int shmid_ = -1;
    int semid_ = -1;
};