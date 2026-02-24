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

  // A: 4x5, B: 5x6, C: 4x6
  std::vector<double> A, B, C(4 * 6);
  std::vector<double> transposed(6 * 5); // B^T: 6x5

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

  // ВАЖНО: этот алгоритм ожидает 20 процессов (4*5), иначе всё разваливается.
  if (size != 20)
  {
    if (rank == 0)
      std::cerr << "Ошибка: нужно ровно 20 процессов (4*5). Сейчас: " << size << "\n";
    MPI_Finalize();
    return 1;
  }

  MPI_Comm col_comm{}, row_comm{};
  MPI_Comm_split(MPI_COMM_WORLD, rank % 5, rank, &col_comm); // 5 колонок по 4 процесса
  MPI_Comm_split(MPI_COMM_WORLD, rank / 5, rank, &row_comm); // 4 строки по 5 процессов

  // Каждый процесс получает 1 элемент A
  double element{};
  MPI_Scatter(A.data(), 1, MPI_DOUBLE, &element, 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);

  std::array<double, 6> local{};
  local.fill(0.0);

  // Идём по 6 "столбцам" результата (на самом деле по строкам B^T)
  for (int j = 0; j < 6; ++j)
  {
    // Берём j-ю строку B^T (длина 5)
    double* column_ptr = transposed.data() + j * 5;

    // В каждой "колонке" сетки только процесс с локальным рангом 0 реально имеет данные (rank%5==0),
    // он разошлёт их по col_comm
    if (rank % 5 == 0)
      MPI_Bcast(column_ptr, 5, MPI_DOUBLE, 0, col_comm);
    else
      MPI_Bcast(column_ptr, 5, MPI_DOUBLE, 0, col_comm);

    // В каждой "строке" сетки (row_comm) раздаём по одному элементу столбца
    double other_element{};
    MPI_Scatter(column_ptr, 1, MPI_DOUBLE, &other_element, 1, MPI_DOUBLE, 0, row_comm);

    double prod = element * other_element;

    // Складываем 5 произведений в один результат на root каждой строки (локальный rank 0 в row_comm)
    double result{};
    MPI_Reduce(&prod, &result, 1, MPI_DOUBLE, MPI_SUM, 0, row_comm);

    // Запоминаем только там, где Reduce выдал результат
    int row_rank{};
    MPI_Comm_rank(row_comm, &row_rank);
    if (row_rank == 0)
      local[j] = result;
  }

  // Собираем 4 строки (по 6 чисел) у rank%5==0 через col_comm
  MPI_Gather(local.data(), 6, MPI_DOUBLE, C.data(), 6, MPI_DOUBLE, 0, col_comm);

  MPI_Comm_free(&col_comm);
  MPI_Comm_free(&row_comm);

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