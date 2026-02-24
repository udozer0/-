#include <mpi.h>

#include <array>
#include <iostream>
#include <vector>

int main(int argc, char** argv)
{
  MPI_Init(&argc, &argv);

  int rank{};
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  int size{};
  MPI_Comm_size(MPI_COMM_WORLD, &size);

  // Надо 4 процесса: по одному на строку A (4x5)
  if (size != 4)
  {
    if (rank == 0)
      std::cerr << "Ошибка: нужно ровно 4 процесса (по числу строк A=4). Сейчас: " << size << "\n";
    MPI_Finalize();
    return 1;
  }

  std::vector<double> A, B, C(4 * 6);        // A: 4x5, B: 5x6, C: 4x6
  std::vector<double> transposed(6 * 5);     // B^T: 6x5

  if (rank == 0)
  {
    A.resize(4 * 5);
    B.resize(5 * 6);

    // A = 1..20
    for (int i = 0; i < 4 * 5; ++i) A[i] = i + 1;

    // B = 1..30
    for (int i = 0; i < 5 * 6; ++i) B[i] = i + 1;

    // transpose B into transposed (6x5): transposed[j*5 + i] = B[i*6 + j]
    for (int i = 0; i < 5; ++i)
      for (int j = 0; j < 6; ++j)
        transposed[j * 5 + i] = B[i * 6 + j];

    std::cout << "Матрица A (4x5):\n";
    for (int r = 0; r < 4; ++r)
    {
      for (int c = 0; c < 5; ++c) std::cout << A[r * 5 + c] << "\t";
      std::cout << "\n";
    }

    std::cout << "Матрица B (5x6):\n";
    for (int r = 0; r < 5; ++r)
    {
      for (int c = 0; c < 6; ++c) std::cout << B[r * 6 + c] << "\t";
      std::cout << "\n";
    }
  }

  // Каждый процесс получает одну строку A (5 чисел)
  std::array<double, 5> row{};
  MPI_Scatter(A.data(), 5, MPI_DOUBLE,
              row.data(), 5, MPI_DOUBLE,
              0, MPI_COMM_WORLD);

  // Локальная строка результата C для этого rank (6 чисел)
  std::array<double, 6> local{};
  local.fill(0.0);

  // Для каждого столбца результата (их 6) берём соответствующую "строку" B^T длины 5
  for (int j = 0; j < 6; ++j)
  {
    double* column_ptr = transposed.data() + j * 5;

    // В исходнике Bcast был по всему миру: оставляем так же
    MPI_Bcast(column_ptr, 5, MPI_DOUBLE, 0, MPI_COMM_WORLD);

    double acc = 0.0;
    for (int k = 0; k < 5; ++k)
      acc += row[k] * column_ptr[k];

    local[j] = acc;
  }

  // Собираем 4 строки результата (по 6 чисел) на rank 0
  MPI_Gather(local.data(), 6, MPI_DOUBLE,
             C.data(), 6, MPI_DOUBLE,
             0, MPI_COMM_WORLD);

  if (rank == 0)
  {
    std::cout << "Матрица C (4x6):\n";
    for (int r = 0; r < 4; ++r)
    {
      for (int c = 0; c < 6; ++c) std::cout << C[r * 6 + c] << "\t";
      std::cout << "\n";
    }
  }

  MPI_Finalize();
  return 0;
}