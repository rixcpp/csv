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

  void test_parse_single_row()
  {
    const rixlib::csv::Table table =
        rixlib::csv::parse("name,language,level\n");

    expect_true(table.size() == 1, "parse should return 1 row");
    expect_true(table[0].size() == 3, "row should contain 3 fields");
    expect_true(table[0][0] == "name", "first field should be name");
    expect_true(table[0][1] == "language", "second field should be language");
    expect_true(table[0][2] == "level", "third field should be level");
  }

  void test_parse_table()
  {
    const std::string input =
        "name,language\n"
        "Ada,C++\n"
        "Gaspard,Vix\n";

    const rixlib::csv::Table table = rixlib::csv::parse(input);

    expect_true(table.size() == 3, "parse should return 3 rows");
    expect_true(table[0].size() == 2, "header row should contain 2 fields");
    expect_true(table[1][0] == "Ada", "second row first field should be Ada");
    expect_true(table[1][1] == "C++", "second row second field should be C++");
    expect_true(table[2][0] == "Gaspard", "third row first field should be Gaspard");
    expect_true(table[2][1] == "Vix", "third row second field should be Vix");
  }

  void test_parse_skips_empty_lines_when_enabled()
  {
    const std::string input =
        "name,language\n"
        "\n"
        "Ada,C++\n";

    rixlib::csv::Options options{};
    options.skip_empty_lines = true;

    const rixlib::csv::Table table = rixlib::csv::parse(input, options);

    expect_true(table.size() == 2, "parse should skip empty lines when enabled");
    expect_true(table[1][0] == "Ada", "remaining data row should be Ada");
  }

  void test_write_row()
  {
    const rixlib::csv::Row row = {"Ada", "C++", "expert"};
    const std::string output = rixlib::csv::write_row(row);

    expect_true(output == "Ada,C++,expert", "write_row should join fields with comma");
  }

  void test_write_table()
  {
    const rixlib::csv::Table table = {
        {"name", "language"},
        {"Ada", "C++"},
        {"Gaspard", "Vix"},
    };

    const std::string output = rixlib::csv::write(table);

    const std::string expected =
        "name,language\n"
        "Ada,C++\n"
        "Gaspard,Vix\n";

    expect_true(output == expected, "write should serialize table to CSV text");
  }

  void run_tests()
  {
    test_parse_single_row();
    test_parse_table();
    test_parse_skips_empty_lines_when_enabled();
    test_write_row();
    test_write_table();
  }
}

int main()
{
  run_tests();

  std::cout << "csv tests passed\n";
  return 0;
}
