#include "messages.hpp"
#include "shared_memory.hpp"
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>

constexpr auto protocol_dir = "protocols";

class Queue
{
public:
    Queue(int capacity) : shared_(capacity), capacity_(capacity) {}

    Message Push(Message &elem)
    {
        auto session = shared_.OpenSession();

        if (*session.GetMessages()->current_size >= capacity_)
        {
            elem.status = REJECTED;
            return elem;
        }

        session.GetMessages()->messages[(*session.GetMessages()->current_size)++] = elem;

        return elem;
    }

    std::optional<Message> Pop(const GasType type)
    {
        auto session = shared_.OpenSession();
        auto messages = session.GetMessages();
        for (auto i = 0; i < *messages->current_size; ++i)
        {
            if (messages->messages[i].gas_type == type && messages->messages[i].status == EXPECTED)
            {
                messages->messages[i].status = INWORK;
                const auto mes = messages->messages[i];
                Remove(messages->messages, i, messages->current_size);
                return mes;
            }
        }

        return {};
    }

private:
    void Remove(Message *mes, int index, int *size)
    {
        for (auto i = index; i < *size; ++i)
        {
            mes[i] = mes[i + 1];
        }
        (*size)--;
    }

private:
    int capacity_;
    SharedMemory shared_;
};