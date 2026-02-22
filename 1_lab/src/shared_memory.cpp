#include "shared_memory.hpp"

SharedMemory::SharedMemory(int capacity) : capacity_(capacity) {
    std::filesystem::create_directory("protocols");

    const char* key_file = "protocols/queue.key";
    std::ofstream{key_file};                     // создаём файл для ftok
    key_ = ftok(key_file, 'Q');
    if (key_ == -1) throw std::runtime_error("ftok failed");

    size_t mem_size = sizeof(int) + capacity * sizeof(Message);
    shmid_ = shmget(key_, mem_size, IPC_CREAT | 0666);
    if (shmid_ == -1) throw std::runtime_error("shmget failed");

    void* ptr = shmat(shmid_, nullptr, 0);
    if (ptr == (void*)-1) throw std::runtime_error("shmat failed");
    queue_ptr_ = static_cast<SharedQueue*>(ptr);
    queue_ptr_->size = 0;

    semid_ = semget(key_, 5, IPC_CREAT | 0666);
    if (semid_ == -1) throw std::runtime_error("semget failed");

    unsigned short vals[5] = {1, (unsigned short)capacity, 0, 0, 0};
    union semun arg;
    arg.array = vals;
    if (semctl(semid_, 0, SETALL, arg) == -1)
        throw std::runtime_error("semctl SETALL failed");
}

SharedMemory::~SharedMemory() {
    if (queue_ptr_) shmdt(queue_ptr_);
    if (shmid_ != -1) shmctl(shmid_, IPC_RMID, nullptr);
    if (semid_ != -1) semctl(semid_, 0, IPC_RMID, 0);
}