#include <iomanip>
#include <iostream>
#include <random>
#include <ranges>

using namespace std::ranges;
using Matrix = std::vector<int>;

template <class T> T GetRandom(double mean, double stddev = 20)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::normal_distribution<double> dist(mean, stddev);

    return dist(gen);
}

inline void Print(const Matrix& matrix, int m)
{
    if (m <= 0) return; // чтоб не делить на ноль и не страдать

    int col = 0;
    for (const auto& j : matrix)
    {
        std::cout << std::setw(3) << j << " ";
        ++col;

        if (col == m)
        {
            std::cout << '\n';
            col = 0;
        }
    }

    // если последняя строка была неполной, добиваем перевод строки
    if (col != 0)
        std::cout << '\n';
}

inline auto MakeMatrix(int n, int m)
{
    Matrix matrix(n * m);

    for (auto i : views::iota(0, n * m))
    {
        matrix[i] = (i + 1) * 2 / 3 + 1;
    }

    return matrix;
}