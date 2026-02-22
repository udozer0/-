#include "queue.hpp"

Queue::Queue(int capacity) : shm_(capacity), capacity_(capacity) {
    semid_ = shm_.getSemid();

    sops[0] = {0, -1, 0};   // MUTEX
    sops[1] = {1, -1, 0};   // EMPTY
    sops[2] = {2, -1, 0};   // 76
    sops[3] = {3, -1, 0};   // 92
    sops[4] = {4, -1, 0};   // 95
}

void Queue::P(int sem_num) {
    if (semop(semid_, &sops[sem_num], 1) == -1)
        throw std::runtime_error("semop P failed");
}

void Queue::V(int sem_num) {
    sops[sem_num].sem_op = 1;
    P(sem_num);
}

bool Queue::Push(Message msg) {
    P(1);           // EMPTY--
    P(0);           // MUTEX

    auto* q = shm_.attach();
    if (q->size >= capacity_) {
        V(0);
        V(1);
        return false;
    }

    q->messages[q->size++] = msg;
    V(0);           // MUTEX

    int sem = (msg.gas_type == AI76) ? 2 : (msg.gas_type == AI92) ? 3 : 4;
    V(sem);         // сигнал нужной колонке
    return true;
}

std::optional<Message> Queue::Pop(GasType type) {
    int sem = (type == AI76) ? 2 : (type == AI92) ? 3 : 4;
    P(sem);         // ждём машину своего типа
    P(0);           // MUTEX

    auto* q = shm_.attach();
    for (int i = 0; i < q->size; ++i) {
        if (q->messages[i].gas_type == type && q->messages[i].status == EXPECTED) {
            Message mes = q->messages[i];
            mes.status = INWORK;

            // сдвиг очереди
            for (int j = i; j < q->size - 1; ++j)
                q->messages[j] = q->messages[j + 1];
            q->size--;

            V(1);       // EMPTY++
            V(0);       // MUTEX
            return mes;
        }
    }

    V(0);
    return {};
}