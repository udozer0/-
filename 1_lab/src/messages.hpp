#ifndef MESSAGES_HPP
#define MESSAGES_HPP

#include <ostream>
#include <string_view>
enum GasType
{
    AI76 = 0,
    AI92 = 1,
    AI95 = 2
};

inline std::string_view ToString(GasType type)
{
    switch (type)
    {
    case AI76:
        return "AI76";
    case AI92:
        return "AI92";
    case AI95:
        return "AI95";
    }

    return "";
}

enum Status
{
    PROCESSED = 0,
    EXPECTED = 1,
    REJECTED = 2,
    INWORK = 3
};

inline std::string_view ToString(Status status)
{
    switch (status)
    {
    case PROCESSED:
        return "Обработан";
    case EXPECTED:
        return "Ожидается";
    case REJECTED:
        return "Отклонен";
    case INWORK:
        return "В работе";
    }

    return "";
}

struct Message
{
    int id{-1};
    GasType gas_type;
    Status status{EXPECTED};
};

inline std::ostream &operator<<(std::ostream &out, const Message &mes)
{
    out << "{id: " << mes.id << ", Station: " << ToString(mes.gas_type) << ", Status: " << ToString(mes.status) << "}";

    return out;
}

struct StoredMessages
{
    int *current_size;
    Message *messages;
};

#endif