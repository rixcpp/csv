/**
 * @file csv.hpp
 * @brief Small CSV reader and writer utilities for Rix.
 *
 * This header provides the independent CSV component used by the global
 * Rix facade package.
 *
 * The package `@rix/csv` does not expose the global `rix` object.
 * It exposes `rixlib::csv::Csv`, which can later be mounted by `@rix/rix`
 * as `rix.csv`.
 *
 * @author Gaspard Kirira
 */

#ifndef RIXCPP_CSV_INCLUDE_RIX_CSV_HPP_INCLUDED
#define RIXCPP_CSV_INCLUDE_RIX_CSV_HPP_INCLUDED

#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rixlib::csv
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
   * @brief CSV reader and writer component.
   *
   * This class is intentionally stateless. It can be used directly from
   * `@rix/csv`, and later embedded inside the global Rix facade as `rix.csv`.
   */
  class Csv
  {
  public:
    /**
     * @brief Parses one CSV line into fields.
     *
     * This minimal parser splits fields using a comma.
     *
     * @param line The CSV line to parse.
     * @return A row containing the parsed fields.
     */
    [[nodiscard]] Row parse_line(std::string_view line) const
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
    [[nodiscard]] Table parse(std::string_view text) const
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
    [[nodiscard]] std::string write_line(const Row &row) const
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
    [[nodiscard]] std::string write(const Table &table) const
    {
      std::string output;

      for (const auto &row : table)
      {
        output += write_line(row);
        output += '\n';
      }

      return output;
    }
  };
}

#endif // RIXCPP_CSV_INCLUDE_RIX_CSV_HPP_INCLUDED
