#include "messages.hpp"
#include "semaphore.hpp"

class Session
{
public:
    Session(StoredMessages *mes, SemaphorePtr sem) : messages_(mes), semaphore_(std::move(sem)) { semaphore_->Wait(); }

    ~Session() { semaphore_->Post(); }

    StoredMessages *GetMessages() { return messages_; }

private:
    StoredMessages *messages_;
    SemaphorePtr semaphore_;
};