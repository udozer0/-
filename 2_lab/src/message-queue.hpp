#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <cstring>
#include <iostream>
#include <mqueue.h>
#include <stdexcept>
#include <string>

class MessageQueue
{
public:
    MessageQueue(const std::string &queueName, bool isOwner = false, size_t maxMsg = 10, size_t msgSize = 256)
        : queueName(queueName), msgSize(msgSize), isOwner(isOwner)
    {

        struct mq_attr attr
        {
        };
        attr.mq_flags = 0;
        attr.mq_maxmsg = maxMsg;
        attr.mq_msgsize = msgSize;
        attr.mq_curmsgs = 0;

        int flags = isOwner ? (O_CREAT | O_RDWR) : O_RDWR;
        mq = mq_open(this->queueName.c_str(), flags, 0644, isOwner ? &attr : nullptr);
        if (mq == -1)
        {
            throw std::runtime_error("mq_open failed: " + std::string(strerror(errno)));
        }
    }

    ~MessageQueue() { mq_close(mq); }

    void send(const std::string &message, unsigned priority = 0)
    {
        if (mq_send(mq, message.c_str(), message.size() + 1, priority) == -1)
        {
            throw std::runtime_error("mq_send failed: " + std::string(strerror(errno)));
        }
    }
    std::string receive(unsigned *priority = nullptr)
    {
        char buffer[msgSize];
        unsigned prio;

        ssize_t bytesRead = mq_receive(mq, buffer, msgSize, &prio);
        if (bytesRead == -1)
        {
            throw std::runtime_error("mq_receive failed: " + std::string(strerror(errno)));
        }

        if (priority)
        {
            *priority = prio;
        }

        return std::string(buffer);
    }

private:
    std::string queueName;
    mqd_t mq;
    size_t msgSize;
    bool isOwner;
};

#endif // MESSAGE_QUEUE_H
