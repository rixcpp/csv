/**
 * @file csv.hpp
 * @brief Small CSV reader and writer utilities for Rix.
 *
 * This header provides a minimal CSV API for parsing CSV text into rows
 * and writing rows back into CSV text.
 *
 * @author Gaspard Kirira
 */

#ifndef RIX_CSV_HPP
#define RIX_CSV_HPP

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rix::csv
{
  /**
   * @brief Represents one CSV row.
   */
  using Row = std::vector<std::string>;

  /**
   * @brief Represents a full CSV table.
   */
  using Table = std::vector<Row>;

  /**
   * @brief Parses one CSV line into fields.
   *
   * This minimal parser splits fields using a comma.
   *
   * @param line The CSV line to parse.
   * @return A row containing the parsed fields.
   */
  inline Row parse_line(std::string_view line)
  {
    Row row;
    std::string field;
    std::stringstream stream{std::string(line)};

    while (std::getline(stream, field, ','))
    {
      row.push_back(field);
    }

    return row;
  }

  /**
   * @brief Parses CSV text into a table.
   *
   * Empty lines are ignored.
   *
   * @param text The CSV text to parse.
   * @return A table containing all parsed rows.
   */
  inline Table parse(std::string_view text)
  {
    Table table;
    std::string line;
    std::stringstream stream{std::string(text)};

    while (std::getline(stream, line))
    {
      if (!line.empty())
      {
        table.push_back(parse_line(line));
      }
    }

    return table;
  }

  /**
   * @brief Writes one CSV row into a CSV line.
   *
   * @param row The row to write.
   * @return A CSV line.
   */
  inline std::string write_line(const Row &row)
  {
    std::string output;

    for (std::size_t i = 0; i < row.size(); ++i)
    {
      if (i > 0)
      {
        output += ',';
      }

      output += row[i];
    }

    return output;
  }

  /**
   * @brief Writes a CSV table into CSV text.
   *
   * @param table The table to write.
   * @return CSV text.
   */
  inline std::string write(const Table &table)
  {
    std::string output;

    for (const auto &row : table)
    {
      output += write_line(row);
      output += '\n';
    }

    return output;
  }
}

#endif // RIX_CSV_HPP
