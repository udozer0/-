#include "messages.hpp"
#include "session.hpp"
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <unistd.h>

class SharedMemory
{
public:
    SharedMemory(int size) : semaphore_(std::make_shared<Semaphore>(SEMAPHORE_NAME))
    {
        std::ofstream{SHARED_MEMORY_NAME};

        const auto key = ftok(SHARED_MEMORY_NAME, 652);
        if (key == -1)
        {
            throw std::runtime_error("Не удалось создать уникальный ключ");
        }

        shm_fd_ = shmget(key, sizeof(Message) * size + sizeof(int) + 1, 0666 | IPC_CREAT);
        if (shm_fd_ == -1)
        {
            throw std::runtime_error("Не удалось открыть объект разделяемой памяти");
        }

        // Отображение разделяемой памяти в адресное пространство процесса
        auto memory = shmat(shm_fd_, nullptr, 0);
        if (memory == MAP_FAILED)
        {
            throw std::runtime_error("Не удалось отобразить разделяемую память");
        }

        int *size_queue = reinterpret_cast<int *>(memory);

        *size_queue = 0;
        Message *messages = reinterpret_cast<Message *>(size_queue + 1);

        messages_ = StoredMessages{size_queue, messages};
    }

    ~SharedMemory()
    {
        std::filesystem::remove(SHARED_MEMORY_NAME);
        shmdt(messages_.current_size);
        shmctl(shm_fd_, IPC_RMID, nullptr);
    }

    Session OpenSession() { return Session(&messages_, semaphore_); }

private:
    StoredMessages messages_;
    int shm_fd_{-1};
    SemaphorePtr semaphore_;
};