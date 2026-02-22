#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <format>
#include <iostream>
#include <limits>
#include <mpi.h>
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

    std::array<int, 2> dims{}, periods{};
    MPI_Dims_create(size, dims.size(), dims.data());

    MPI_Comm communicator{};
    MPI_Cart_create(MPI_COMM_WORLD, dims.size(), dims.data(), periods.data(), 0, &communicator);

    std::array<int, 2> coordinates{};
    MPI_Cart_coords(communicator, rank, dims.size(), coordinates.data());

    const auto &[x, y] = coordinates;

    int max = 0, min = INT_MAX;

    MPI_Comm start_row{};
    // Группа процессоров в первой строке решетки
    MPI_Comm_split(communicator, x == 0, 0, &start_row);
    // Рассылаем поэлементно массив первой строке процессоров, записываем в max
    MPI_Scatter(array.data(), 1, MPI_INT, &max, 1, MPI_INT, 0, start_row);

    if (x <= y)
    {
        int up, down, left, right;

        MPI_Cart_shift(communicator, 0, 1, &up, &down);
        MPI_Cart_shift(communicator, 1, 1, &left, &right);

        // Принимаем max сверху
        if (x != 0)
            MPI_Recv(&max, 1, MPI_INT, up, 0, communicator, MPI_STATUS_IGNORE);

        // Принимаем min слева
        if (x != y)
            MPI_Recv(&min, 1, MPI_INT, left, 0, communicator, MPI_STATUS_IGNORE);

        if (min > max)
            std::swap(min, max);

        // Отправляем max вниз
        if (down > 0)
            MPI_Send(&max, 1, MPI_INT, down, 0, communicator);

        // Отправляем min направо
        MPI_Send(&min, 1, MPI_INT, std::max(right, 0), 0, communicator);
    }

    if (rank == 0)
    {
        // Собираем результат с последнего столбца
        std::vector<int> sources = {4, 9, 14, 19, 24};

        for (auto i = 0; i < array.size(); ++i)
        {
            MPI_Recv(&array[i], 1, MPI_INT, sources[i], 0, communicator, MPI_STATUS_IGNORE);
        }

        std::cout << "Результирующий массив: \n";
        for (auto const &element : array)
            std::cout << element << " ";
        std::cout << std::endl;
    }

    MPI_Finalize();
}