#include <climits>
#include <mpi.h>

#include <array>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int size{};
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int rank{};
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    std::vector<int> array{5, 4, 3, 2, 1};

    if (rank == 0)
    {
        std::cout << "Массив:\n";
        for (auto const &element : array)
            std::cout << element << " ";
        std::cout << std::endl;
    }

    std::array<int, 1> dims{5}, periods{};

    MPI_Comm communicator{};
    MPI_Cart_create(MPI_COMM_WORLD, dims.size(), dims.data(), periods.data(), 0, &communicator);

    int up, down;
    MPI_Cart_shift(communicator, 0, 1, &up, &down);

    int max = 0, min = INT_MAX;

    for (auto const &value : array)
    {
        // Принимаем сверху max
        rank == 0 ? max = value : MPI_Recv(&max, 1, MPI_INT, up, 0, communicator, MPI_STATUS_IGNORE);

        // Оставляем у себя минимальный элемент
        if (min > max)
            std::swap(min, max);

        // Отправляем max вниз
        if (down > 0)
            MPI_Send(&max, 1, MPI_INT, down, 0, communicator);
    }

    // Отправляем минимальный элемент в результирующий массив
    MPI_Gather(&min, 1, MPI_INT, array.data(), 1, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == 0)
    {
        std::cout << "Результирующий массив: \n";
        for (auto const &element : array)
            std::cout << element << " ";
        std::cout << std::endl;
    }

    MPI_Finalize();
}