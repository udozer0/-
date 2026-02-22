#include <mpi.h>

#include <iostream>
#include <ranges>
#include <vector>

#include "matrix/matrix-helper.hpp"

int main(int argc, char **argv)
{
    using namespace std::views;

    MPI_Init(&argc, &argv);

    int size{};
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    int rank{};
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    std::vector<int> matrix(size * size), vector(size), column(size);

    if (rank == 0)
    {
        matrix = MakeMatrix(size, size);

        vector = MakeMatrix(size, 1);

        std::cout << "Матрица\n";
        Print(matrix, size);

        std::cout << "Вектор\n";
        Print(vector, 1);
    }

    // Рассылаем строки матрицы процессорным элементам
    MPI_Scatter(matrix.data(), size, MPI_INT, column.data(), size, MPI_INT, 0, MPI_COMM_WORLD);

    int vector_element;
    // Рассылаем элементы вектора процессорным элементам
    MPI_Scatter(vector.data(), 1, MPI_INT, &vector_element, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // У каждого процессорного элемента строка матрицы и элемент вектора.
    // Находим произведение строки на элемент
    std::vector<int> multiplication;
    for (auto i : views::iota(0, size))
        multiplication.emplace_back(column[i] * vector_element);

    MPI_Comm communicator{};
    std::array<int, 1> dims{5}, periods{1};
    MPI_Cart_create(MPI_COMM_WORLD, 1, dims.data(), periods.data(), 0, &communicator);

    int prev, next;
    MPI_Cart_shift(communicator, 0, 1, &prev, &next);

    std::vector<int> results(size);

    // Ищем сумму вектора multiplication
    for (auto i : views::iota(0, size))
    {
        int previous = 0;

        // Принимаем результат от предыдущего процессорного элемента
        if (rank != 0)
            MPI_Recv(&previous, 1, MPI_INT, prev, 0, communicator, MPI_STATUS_IGNORE);

        const auto result = previous + multiplication[i];

        // Отправляем сумму предыдущего результата и произведения следующему процессорному элементу
        MPI_Send(&result, 1, MPI_INT, next, 0, communicator);

        // Принимаем результат
        if (rank == 0)
            MPI_Recv(&results[i], 1, MPI_INT, prev, 0, communicator, MPI_STATUS_IGNORE);
    }

    if (rank == 0)
    {
        std::cout << "Результирующий массив: \n";
        for (auto const &element : results)
            std::cout << element << " ";
        std::cout << std::endl;
    }

    MPI_Finalize();
}