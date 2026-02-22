#pragma once
#include "messages.hpp"
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>
#include <filesystem>

struct SharedQueue {
    int size;
    Message messages[1];   // flexible array
};

class SharedMemory {
public:
    SharedMemory(int capacity);
    ~SharedMemory();

    SharedQueue* attach() { return queue_ptr_; }
    int getSemid() const { return semid_; }

private:
    key_t key_ = -1;
    int shmid_ = -1;
    int semid_ = -1;
    SharedQueue* queue_ptr_ = nullptr;
    int capacity_;
};