
#include "matrix-helper.hpp"
#include <mpi.h>

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

    int rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    Matrix matrixA, matrixB, matrixC;
    std::vector<int> transposedB, result_row(COLUMN_RES), columnB, rowA(COLUMN_A);

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

    // Рассылаем строки матрицы А
    MPI_Scatter(matrixA.data(), COLUMN_A, MPI_INT, rowA.data(), COLUMN_A, MPI_INT, 0, MPI_COMM_WORLD);

    // Расчет результирующей строки

    for (auto index_res : views::iota(0, COLUMN_B))
    {
        columnB = rank ? std::vector<int>(COLUMN_B)
                       : std::vector(transposedB.data() + index_res * ROW_B,
                                     transposedB.data() + index_res * ROW_B + COLUMN_B);

        // Рассылаем cтроку В
        MPI_Bcast(columnB.data(), COLUMN_B, MPI_INT, 0, MPI_COMM_WORLD);

        for (auto index : views::iota(0, COLUMN_A))
        {
            result_row[index_res] += rowA[index] * columnB[index];
        }
    }

    // Сбор результата
    MPI_Gather(result_row.data(), COLUMN_RES, MPI_INT, matrixC.data(), COLUMN_RES, MPI_INT, 0, MPI_COMM_WORLD);

    if (rank == ROOT_RANK)
    {
        std::cout << "Результат: " << std::endl;
        Print(matrixC, COLUMN_RES);
    }

    MPI_Finalize();
    return 0;
}
