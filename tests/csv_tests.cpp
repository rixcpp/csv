/**
 * @file csv_tests.cpp
 * @brief Basic tests for rix/csv.
 *
 * @author Gaspard Kirira
 */

#include <rix/csv.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
  void expect_true(bool condition, const std::string &message)
  {
    if (!condition)
    {
      std::cerr << "FAILED: " << message << '\n';
      std::exit(1);
    }
  }

  void test_parse_line()
  {
    const rixlib::csv::Csv csv;
    const rixlib::csv::Row row = csv.parse_line("name,language,level");

    expect_true(row.size() == 3, "parse_line should return 3 fields");
    expect_true(row[0] == "name", "first field should be name");
    expect_true(row[1] == "language", "second field should be language");
    expect_true(row[2] == "level", "third field should be level");
  }

  void test_parse_table()
  {
    const std::string input =
        "name,language\n"
        "Ada,C++\n"
        "Gaspard,Vix\n";

    const rixlib::csv::Csv csv;
    const rixlib::csv::Table table = csv.parse(input);

    expect_true(table.size() == 3, "parse should return 3 rows");
    expect_true(table[0].size() == 2, "header row should contain 2 fields");
    expect_true(table[1][0] == "Ada", "second row first field should be Ada");
    expect_true(table[1][1] == "C++", "second row second field should be C++");
    expect_true(table[2][0] == "Gaspard", "third row first field should be Gaspard");
    expect_true(table[2][1] == "Vix", "third row second field should be Vix");
  }

  void test_parse_ignores_empty_lines()
  {
    const std::string input =
        "name,language\n"
        "\n"
        "Ada,C++\n";

    const rixlib::csv::Csv csv;
    const rixlib::csv::Table table = csv.parse(input);

    expect_true(table.size() == 2, "parse should ignore empty lines");
    expect_true(table[1][0] == "Ada", "remaining data row should be Ada");
  }

  void test_write_line()
  {
    const rixlib::csv::Csv csv;
    const rixlib::csv::Row row = {"Ada", "C++", "expert"};
    const std::string output = csv.write_line(row);

    expect_true(output == "Ada,C++,expert", "write_line should join fields with comma");
  }

  void test_write_table()
  {
    const rixlib::csv::Csv csv;

    const rixlib::csv::Table table = {
        {"name", "language"},
        {"Ada", "C++"},
        {"Gaspard", "Vix"},
    };

    const std::string output = csv.write(table);

    const std::string expected =
        "name,language\n"
        "Ada,C++\n"
        "Gaspard,Vix\n";

    expect_true(output == expected, "write should serialize table to CSV text");
  }

  void run_tests()
  {
    test_parse_line();
    test_parse_table();
    test_parse_ignores_empty_lines();
    test_write_line();
    test_write_table();
  }
}

int main()
{
  run_tests();

  std::cout << "csv tests passed\n";
  return 0;
}
