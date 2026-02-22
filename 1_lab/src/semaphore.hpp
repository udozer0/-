#include <fcntl.h> // For O_CREAT, O_EXCL
#include <iostream>
#include <memory>
#include <semaphore.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h> // For mode constants

class Semaphore
{
public:
    Semaphore(const std::string &name, unsigned int initial_value = 1) : sem_name(name), sem_ptr(nullptr)
    {
        sem_ptr = sem_open(name.c_str(), O_CREAT, 0777, 1);
        if (sem_ptr == SEM_FAILED)
        {

            throw std::runtime_error("Failed to create semaphore: " + name);
        }
    }

    Semaphore &operator=(const Semaphore &) = default;

    // Open existing semaphore
    static Semaphore Open(const std::string &name)
    {
        sem_t *sem_ptr = sem_open(name.c_str(), 0);
        if (sem_ptr == SEM_FAILED)
        {
            throw std::runtime_error("Failed to open semaphore: " + name);
        }
        return Semaphore(name, sem_ptr);
    }

    void Wait()
    {
        if (sem_wait(sem_ptr) != 0)
        {
            throw std::runtime_error("Failed to wait on semaphore: " + sem_name);
        }
    }

    void Post()
    {
        if (sem_post(sem_ptr) != 0)
        {
            throw std::runtime_error("Failed to post on semaphore: " + sem_name);
        }
    }

    void Close()
    {
        if (sem_ptr && sem_close(sem_ptr) != 0)
        {
            throw std::runtime_error("Failed to close semaphore: " + sem_name);
        }
        sem_ptr = nullptr;
    }

    void Unlink() { sem_unlink(sem_name.c_str()); }

    ~Semaphore()
    {
        try
        {

            Close();
            Unlink();
        }
        catch (const std::exception &e)
        {
            std::cerr << e.what() << std::endl;
        }
    }

private:
    std::string sem_name;
    sem_t *sem_ptr;

    // Private constructor for Open function
    Semaphore(const std::string &name, sem_t *existing_sem_ptr) : sem_name(name), sem_ptr(existing_sem_ptr) {}
};

using SemaphorePtr = std::shared_ptr<Semaphore>;