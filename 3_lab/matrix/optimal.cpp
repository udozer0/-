
#include "matrix-helper.hpp"
#include <mpi.h>
#include <vector>

#define ROW_A 4
#define COLUMN_A 5
#define ROW_B COLUMN_A
#define COLUMN_B 6
#define ROW_RES ROW_A
#define COLUMN_RES COLUMN_B

#define ROOT_RANK 0

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);

    int rank, new_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    MPI_Comm row_nodes{};
    MPI_Comm_split(MPI_COMM_WORLD, rank / 5, rank, &row_nodes);

    MPI_Comm column_nodes{};
    MPI_Comm_split(MPI_COMM_WORLD, rank % 5, rank, &column_nodes);

    Matrix matrixA, matrixB, matrixC;
    std::vector<int> transposedB, result_row(COLUMN_RES), rowA(ROW_A);

    int elemA, elemB;

    if (rank == ROOT_RANK)
    {
        matrixA = MakeMatrix(ROW_A, COLUMN_A);
        matrixB = MakeMatrix(ROW_B, COLUMN_B);
        matrixC.assign(ROW_RES * COLUMN_RES, 0);
        transposedB.assign(ROW_B * COLUMN_B, 0);

        for (int i = 0; i < ROW_B; ++i)
            for (int j = 0; j < COLUMN_B; ++j)
                transposedB[j * ROW_B + i] = matrixB[i * COLUMN_B + j];

        std::cout << "Матрица А\n";
        Print(matrixA, COLUMN_A);
        std::cout << "Матрица B\n";
        Print(matrixB, COLUMN_B);
    }

    // Рассылаем элементы матрицы А
    MPI_Scatter(matrixA.data(), 1, MPI_INT, &elemA, 1, MPI_INT, 0, MPI_COMM_WORLD);

    // Расчет результирующей строки
    for (auto index_res : views::iota(0, COLUMN_B))
    {
        std::vector<int> columnB;

        if (rank % 5 == 0)
        {
            columnB = rank ? std::vector<int>(COLUMN_B)
                           : std::vector(transposedB.data() + index_res * ROW_B,
                                         transposedB.data() + index_res * ROW_B + COLUMN_B);

            // Рассылаем cтроку В 4 главным узлам
            MPI_Bcast(columnB.data(), COLUMN_B, MPI_INT, 0, column_nodes);
        }

        // Рассылаем элементы строки B побочным узлам
        MPI_Scatter(columnB.data(), 1, MPI_INT, &elemB, 1, MPI_INT, 0, row_nodes);

        auto sum = elemA * elemB;

        // Формируем элемент результирующей строки, путем складывания произведений элементов строки А и столбца В
        MPI_Reduce(&sum, &result_row[index_res], 1, MPI_INT, MPI_SUM, 0, row_nodes);
    }

    // Сбор результата (здесь 0 -- ROOT_RANK)
    if (rank % 5 == 0)
        MPI_Gather(result_row.data(), COLUMN_RES, MPI_INT, matrixC.data(), COLUMN_RES, MPI_INT, 0, column_nodes);

    if (rank == ROOT_RANK)
    {
        std::cout << "Результат: " << std::endl;
        Print(matrixC, COLUMN_RES);
    }

    MPI_Finalize();
    return 0;
}
