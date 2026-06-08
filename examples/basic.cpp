/**
 * @file basic.cpp
 * @brief Basic example for rix/csv.
 */

#include <rix/csv.hpp>

#include <iostream>
#include <string>

int main()
{
  const rixlib::csv::Csv csv{};

  const std::string input =
      "name,language\n"
      "Ada,C++\n"
      "Gaspard,Vix\n";

  const rixlib::csv::Table table = csv.parse(input);

  std::cout << "rows: " << table.size() << '\n';

  for (const auto &row : table)
  {
    for (std::size_t i = 0; i < row.size(); ++i)
    {
      if (i > 0)
      {
        std::cout << ' ';
      }

      std::cout << row[i];
    }

    std::cout << '\n';
  }

  const std::string output = csv.write(table);

  std::cout << "\nserialized:\n";
  std::cout << output;

  return 0;
}
