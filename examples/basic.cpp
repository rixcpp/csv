/**
 * @file basic.cpp
 * @brief Basic example for rix/csv.
 */

#include <rix/csv.hpp>

#include <iostream>
#include <string>

namespace
{
  void print_table(const rixlib::csv::Table &table)
  {
    for (const auto &row : table)
    {
      for (const auto &field : row)
      {
        std::cout << field << ' ';
      }

      std::cout << '\n';
    }
  }

  void run_basic_example()
  {
    const std::string input =
        "name,language\n"
        "Ada,C++\n"
        "Gaspard,Vix\n";

    const rixlib::csv::Csv csv;
    const auto table = csv.parse(input);

    print_table(table);
  }
}

int main()
{
  run_basic_example();
  return 0;
}
