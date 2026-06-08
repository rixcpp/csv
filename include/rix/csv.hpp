/**
 *
 *  @file csv.hpp
 *  @author Gaspard Kirira
 *
 *  Copyright 2025, Gaspard Kirira. All rights reserved.
 *  https://github.com/rixcpp/csv
 *  Use of this source code is governed by a MIT license
 *  that can be found in the License file.
 *
 *  Rix
 *
 *  Header-only CSV parser and writer for rix/csv.
 *
 */

#ifndef RIXCPP_CSV_INCLUDE_RIX_CSV_HPP_INCLUDED
#define RIXCPP_CSV_INCLUDE_RIX_CSV_HPP_INCLUDED

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <ios>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

/// Major version component of rix/csv.
#define RIXCPP_CSV_VERSION_MAJOR 2

/// Minor version component of rix/csv.
#define RIXCPP_CSV_VERSION_MINOR 0

/// Patch version component of rix/csv.
#define RIXCPP_CSV_VERSION_PATCH 0

/// Encoded rix/csv version: major * 10000 + minor * 100 + patch.
#define RIXCPP_CSV_VERSION \
  (RIXCPP_CSV_VERSION_MAJOR * 10000 + RIXCPP_CSV_VERSION_MINOR * 100 + RIXCPP_CSV_VERSION_PATCH)

namespace rixlib::csv
{
  struct Options;
  struct WriteOptions;
  class ParseError;
  class RowView;
  class DictReader;
  class DictWriter;
  class StreamingParser;
  struct Dialect;

  /// A single CSV row: one std::string per field.
  using Row = std::vector<std::string>;

  /// A full CSV document: one Row per logical line.
  using Table = std::vector<Row>;

  /// Satisfied by types that model std::basic_istream<char>.
  template <typename T>
  concept InputStreamLike =
      std::derived_from<std::remove_cvref_t<T>, std::basic_istream<char>>;

  /// Satisfied by callables that accept a const Row& and return a bool-like type.
  template <typename F>
  concept RowPredicate =
      std::invocable<F, const Row &> &&
      std::convertible_to<std::invoke_result_t<F, const Row &>, bool>;

  /// Satisfied by callables that accept a std::string& (in-place field mutator).
  template <typename F>
  concept FieldTransformer = std::invocable<F, std::string &>;

  /**
   * @brief Exception thrown for any malformed CSV input.
   *
   * Derives from std::invalid_argument so legacy catch sites still work.
   * Exposes a 1-based line number and column offset for precise diagnostics.
   */
  class ParseError : public std::invalid_argument
  {
  public:
    /**
     * @param msg   Human-readable description.
     * @param line  1-based line number (0 = position unknown).
     * @param col   1-based column within that line (0 = column unknown).
     */
    explicit ParseError(std::string msg,
                        std::size_t line = 0,
                        std::size_t col = 0)
        : std::invalid_argument(decorate(msg, line, col)), message_(std::move(msg)), line_(line), col_(col)
    {
    }

    /// Raw message without position prefix.
    [[nodiscard]] const std::string &message() const noexcept { return message_; }
    /// 1-based line number (0 = unknown).
    [[nodiscard]] std::size_t line() const noexcept { return line_; }
    /// 1-based column (0 = unknown).
    [[nodiscard]] std::size_t col() const noexcept { return col_; }

  private:
    static std::string decorate(const std::string &msg,
                                std::size_t line, std::size_t col)
    {
      if (line == 0)
        return "csv: " + msg;
      std::string s = "csv:" + std::to_string(line);
      if (col)
        s += ':' + std::to_string(col);
      s += ": " + msg;
      return s;
    }

    std::string message_;
    std::size_t line_;
    std::size_t col_;
  };

  /**
   * @brief Controls every aspect of CSV parsing behaviour.
   *
   * All members are public with sensible defaults matching RFC 4180 and the
   * behaviour of Python's csv.reader.
   *
   * @code
   *   csv::Options opt;
   *   opt.separator        = '\t';   // TSV mode
   *   opt.skip_empty_lines = true;
   *   opt.trim_whitespace  = true;
   *   opt.skip_comments    = true;
   *   opt.comment_char     = '#';
   *   opt.max_field_size   = 1 << 20;   // 1 MiB per field
   * @endcode
   */
  struct Options
  {
    /// Field separator byte.  Default: ','
    char separator = ',';

    /// Quoting byte.  Default: '"'
    char quote_char = '"';

    /// Strip leading/trailing ASCII space and tab from *unquoted* fields.
    /// Quoted fields are never trimmed.  Default: false
    bool trim_whitespace = false;

    /// Silently discard lines whose only content is an empty field.
    /// Default: false  (empty lines produce one row with one empty field)
    bool skip_empty_lines = false;

    /// Silently discard lines whose first non-whitespace byte is comment_char.
    bool skip_comments = false;

    /// The comment-line marker byte.  Only used when skip_comments is true.
    char comment_char = '#';

    /// Maximum bytes in a single field.  0 = unlimited.  Default: 0
    std::size_t max_field_size = 0;

    /// Maximum fields per row.  0 = unlimited.  Default: 0
    std::size_t max_fields_per_row = 0;

    /**
     * @brief Per-field post-processing hook (called after all other transforms).
     *
     * Receives a mutable reference to every field string (including quoted
     * ones) after trimming is applied.  May mutate the string in-place.
     *
     * @code
     *   opt.field_transformer = [](std::string& s){
     *       for (char& c : s) c = static_cast<char>(std::toupper(c));
     *   };
     * @endcode
     */
    std::function<void(std::string &)> field_transformer;

    /**
     * @brief Row-level filter predicate.
     *
     * Called once per committed row after all field transforms.  Return false
     * to drop the row from the result Table.
     *
     * @code
     *   opt.row_filter = [](const csv::Row& r){ return r.size() == 3; };
     * @endcode
     */
    std::function<bool(const Row &)> row_filter;
  };

  /**
   * @brief Controls every aspect of CSV serialisation behaviour.
   *
   * @code
   *   csv::WriteOptions wo;
   *   wo.separator    = ';';
   *   wo.line_ending  = "\r\n";   // RFC 4180 / Windows style
   *   wo.always_quote = true;     // force quotes on every field
   * @endcode
   */
  struct WriteOptions
  {
    /// Field separator byte.  Default: ','
    char separator = ',';

    /// Quoting byte.  Default: '"'
    char quote_char = '"';

    /// Appended after every serialised row.  Default: "\n"
    /// Use "\r\n" for strict RFC 4180 compliance or Windows targets.
    std::string line_ending = "\n";

    /// Quote every field regardless of content.  Default: false
    bool always_quote = false;

    /// Trim leading/trailing ASCII space/tab before deciding whether to quote,
    /// and write the trimmed value.  Default: false
    bool trim_before_write = false;
  };

  namespace detail
  {
    /// Streaming I/O buffer size.  64 KiB balances throughput and stack usage.
    inline constexpr std::size_t kStreamBufSize = 65536u;

    /// Initial reserve size for field buffers.
    inline constexpr std::size_t kFieldReserve = 64u;

    /**
     * @brief Return true for the whitespace bytes stripped by trim_whitespace
     *        (ASCII space 0x20 and horizontal tab 0x09 only).
     */
    [[nodiscard]] inline constexpr bool is_trim_ws(char c) noexcept
    {
      return c == ' ' || c == '\t';
    }

    /**
     * @brief Trim leading and trailing trim-whitespace in-place.
     *        No allocation; operates directly on the string buffer.
     */
    inline void trim_inplace(std::string &s) noexcept
    {
      // Trim trailing
      while (!s.empty() && is_trim_ws(s.back()))
        s.pop_back();
      // Trim leading
      auto first = std::find_if(s.begin(), s.end(),
                                [](char c)
                                { return !is_trim_ws(c); });
      s.erase(s.begin(), first);
    }

    /**
     * @brief Return true when every byte of @p s is a trim-whitespace character.
     */
    [[nodiscard]] inline bool all_whitespace(const std::string &s) noexcept
    {
      return std::all_of(s.begin(), s.end(), is_trim_ws);
    }

    /**
     * @brief Return true when @p s contains no bytes at all.
     */
    [[nodiscard]] inline bool is_empty_string(const std::string &s) noexcept
    {
      return s.empty();
    }

    /**
     * @brief Determine whether a field value needs to be wrapped in quotes.
     *
     * Quoting is required when the field contains the separator, the quote
     * character, a carriage return, or a line feed — or when always_quote is set.
     */
    [[nodiscard]] inline bool field_needs_quoting(const std::string &s,
                                                  const WriteOptions &opt) noexcept
    {
      if (opt.always_quote)
        return true;
      for (const char c : s)
      {
        if (c == opt.separator || c == opt.quote_char ||
            c == '\n' || c == '\r')
          return true;
      }
      return false;
    }

    /**
     * @brief Return the properly escaped CSV representation of a single field.
     *
     * If quoting is required the field is wrapped in opt.quote_char and every
     * internal occurrence of quote_char is doubled (RFC 4180 §2.7).
     * Otherwise the field string is returned unchanged (no allocation).
     */
    [[nodiscard]] inline std::string escape_field(const std::string &s,
                                                  const WriteOptions &opt)
    {
      if (!field_needs_quoting(s, opt))
        return s;

      std::string out;
      out.reserve(s.size() + 2);
      out.push_back(opt.quote_char);
      for (const char c : s)
      {
        if (c == opt.quote_char)
          out.push_back(opt.quote_char);
        out.push_back(c);
      }
      out.push_back(opt.quote_char);
      return out;
    }

    /**
     * @brief Trim @p s then delegate to escape_field.
     *        Used when WriteOptions::trim_before_write is enabled.
     */
    [[nodiscard]] inline std::string escape_field_trimmed(const std::string &s,
                                                          const WriteOptions &opt)
    {
      std::string v = s;
      trim_inplace(v);
      return escape_field(v, opt);
    }

    /**
     * @brief Return true when @p row should be discarded as a comment line.
     *
     * Condition: skip_comments is enabled, the row is non-empty, and the first
     * non-whitespace byte of the first field equals opt.comment_char.
     */
    [[nodiscard]] inline bool is_comment_row(const Row &row,
                                             const Options &opt) noexcept
    {
      if (!opt.skip_comments || row.empty())
        return false;
      for (const char c : row.front())
      {
        if (!is_trim_ws(c))
          return c == opt.comment_char;
      }
      return false;
    }

    /**
     * @brief Return true when every field in @p row is an empty string.
     */
    [[nodiscard]] inline bool is_empty_row(const Row &row) noexcept
    {
      return std::all_of(row.begin(), row.end(), is_empty_string);
    }

    /**
     * @brief Throw ParseError when field size exceeds opt.max_field_size (if set).
     */
    inline void check_field_size(const std::string &field,
                                 const Options &opt,
                                 std::size_t line,
                                 std::size_t col)
    {
      if (opt.max_field_size > 0 && field.size() > opt.max_field_size)
      {
        throw ParseError(
            "field size " + std::to_string(field.size()) +
                " exceeds limit " + std::to_string(opt.max_field_size),
            line, col);
      }
    }

    /**
     * @brief Throw ParseError when row width exceeds opt.max_fields_per_row (if set).
     */
    inline void check_row_width(const Row &row,
                                const Options &opt,
                                std::size_t line)
    {
      if (opt.max_fields_per_row > 0 && row.size() > opt.max_fields_per_row)
      {
        throw ParseError(
            "row has " + std::to_string(row.size()) +
                " fields, exceeds limit " +
                std::to_string(opt.max_fields_per_row),
            line, 0);
      }
    }

    /**
     * @brief Append a serialised Row (no line ending) to @p out.
     *
     * Fields are escaped according to @p opt.  Operates entirely on the
     * existing buffer — no separate allocation per call.
     */
    inline void serialise_row(const Row &row,
                              const WriteOptions &opt,
                              std::string &out)
    {
      for (std::size_t i = 0; i < row.size(); ++i)
      {
        if (i != 0)
          out.push_back(opt.separator);
        if (opt.trim_before_write)
          out += escape_field_trimmed(row[i], opt);
        else
          out += escape_field(row[i], opt);
      }
    }

    /**
     * @brief Byte-at-a-time RFC-4180-conformant CSV parser implemented as an
     *        explicit finite-state machine (FSM).
     *
     * Characters are fed one-at-a-time via push().  After the last byte, call
     * finish() to flush any pending field/row.  Completed rows are either
     * accumulated into `table` (default) or dispatched through the Options
     * row_filter hook, which StreamingParser uses to implement callbacks without
     * storing any rows.
     *
     * Grammar (informal)
     * ──────────────────
     *   csv      := *record
     *   record   := field *( SEP field ) LINEEND
     *   field    := plain | quoted
     *   plain    := *( byte − SEP − CR − LF )
     *   quoted   := QUOTE *( byte | QUOTE QUOTE ) QUOTE
     *   LINEEND  := LF | CR | CR LF
     *
     * States
     * ──────
     *   START_FIELD   Waiting for the first byte of a new field.
     *   IN_PLAIN      Accumulating an unquoted field.
     *   IN_QUOTED     Accumulating a quoted field.
     *   AFTER_QUOTE   Saw a candidate closing-quote in a quoted field.
     *   AFTER_CR      Saw a bare CR; waiting for optional LF (CRLF pair).
     *
     * Memory strategy
     * ───────────────
     * A single `field_` buffer is reused for every field via std::move-on-commit.
     * `current_row_` is similarly moved into `table` on row commit.  This avoids
     * per-field and per-row heap allocations in the common case.
     */
    class StateMachine
    {
    public:
      explicit StateMachine(const Options &opt)
          : opt_(opt)
      {
        field_.reserve(kFieldReserve);
      }

      // The result table (valid after finish() succeeds).
      Table table;

      /**
       * @brief Feed a single byte into the FSM.
       * @throws ParseError on malformed CSV.
       */
      void push(char c)
      {
        ++col_;
        switch (state_)
        {

        case State::START_FIELD:
          if (c == opt_.quote_char)
          {
            is_quoted_ = true;
            state_ = State::IN_QUOTED;
          }
          else if (c == opt_.separator)
          {
            commit_field();
            // remain in START_FIELD
          }
          else if (c == '\n')
          {
            commit_field();
            commit_row();
            reset_col();
          }
          else if (c == '\r')
          {
            commit_field();
            state_ = State::AFTER_CR;
          }
          else
          {
            field_.push_back(c);
            state_ = State::IN_PLAIN;
          }
          break;

        case State::IN_PLAIN:
          if (c == opt_.separator)
          {
            commit_field();
            state_ = State::START_FIELD;
          }
          else if (c == '\n')
          {
            commit_field();
            commit_row();
            reset_col();
            state_ = State::START_FIELD;
          }
          else if (c == '\r')
          {
            commit_field();
            state_ = State::AFTER_CR;
          }
          else
          {
            field_.push_back(c);
          }
          break;

        case State::IN_QUOTED:
          if (c == opt_.quote_char)
          {
            state_ = State::AFTER_QUOTE;
          }
          else
          {
            field_.push_back(c);
          }
          break;

        case State::AFTER_QUOTE:
          if (c == opt_.quote_char)
          {
            // RFC 4180 §2.7: doubled quote → one literal quote_char
            field_.push_back(opt_.quote_char);
            state_ = State::IN_QUOTED;
          }
          else if (c == opt_.separator)
          {
            commit_field();
            state_ = State::START_FIELD;
          }
          else if (c == '\n')
          {
            commit_field();
            commit_row();
            reset_col();
            state_ = State::START_FIELD;
          }
          else if (c == '\r')
          {
            commit_field();
            state_ = State::AFTER_CR;
          }
          else
          {
            // Any other byte after a closing quote is illegal.
            throw ParseError(
                std::string("unexpected byte '") + c +
                    "' after closing quote",
                line_, col_);
          }
          break;

        case State::AFTER_CR:
          if (c == '\n')
          {
            // CRLF: complete the row (field was already committed on CR)
            commit_row();
            reset_col();
            state_ = State::START_FIELD;
          }
          else
          {
            // Bare CR: treat as line ending, re-process c
            commit_row();
            reset_col();
            state_ = State::START_FIELD;
            --col_;
            push(c);
            return;
          }
          break;
        }
      }

      /**
       * @brief Signal end-of-input and flush any pending field/row.
       *
       * Must be called exactly once after all bytes have been fed.
       *
       * @throws ParseError if the input ends inside an unclosed quoted field.
       */
      void finish()
      {
        if (state_ == State::IN_QUOTED)
        {
          throw ParseError(
              "unclosed quoted field at end of input",
              line_, col_);
        }

        // Flush pending content: a non-empty field, or a row that already has
        // committed fields, or the AFTER_QUOTE state (empty quoted field at EOF).
        const bool pending =
            !field_.empty() ||
            !current_row_.empty() ||
            state_ == State::AFTER_QUOTE;

        if (pending)
        {
          commit_field();
          commit_row();
        }
      }

    private:
      enum class State : std::uint8_t
      {
        START_FIELD,
        IN_PLAIN,
        IN_QUOTED,
        AFTER_QUOTE,
        AFTER_CR,
      };

      const Options &opt_;
      State state_ = State::START_FIELD;
      bool is_quoted_ = false;
      std::string field_;
      Row current_row_;
      std::size_t line_ = 1;
      std::size_t col_ = 0;

      void reset_col() noexcept { col_ = 0; }

      /**
       * @brief Commit the current field buffer to current_row_.
       *
       * Applies trimming (unquoted only), size limit check, and the user
       * field_transformer hook before appending.
       */
      void commit_field()
      {
        if (!is_quoted_ && opt_.trim_whitespace)
          trim_inplace(field_);

        check_field_size(field_, opt_, line_, col_);

        if (opt_.field_transformer)
          opt_.field_transformer(field_);

        current_row_.push_back(std::move(field_));
        field_.clear();
        field_.reserve(kFieldReserve);
        is_quoted_ = false;
      }

      /**
       * @brief Commit current_row_ to table (after filtering).
       *
       * Applies comment filter, empty-row filter, row-width check, and the
       * user row_filter hook before appending.
       */
      void commit_row()
      {
        ++line_;

        if (is_comment_row(current_row_, opt_))
        {
          current_row_.clear();
          return;
        }

        if (opt_.skip_empty_lines && is_empty_row(current_row_))
        {
          current_row_.clear();
          return;
        }

        check_row_width(current_row_, opt_, line_ - 1);

        if (opt_.row_filter && !opt_.row_filter(current_row_))
        {
          current_row_.clear();
          return;
        }

        table.push_back(std::move(current_row_));
        current_row_.clear();
      }
    };

    /**
     * @brief Feed a std::string_view into @p machine byte-by-byte.
     *        No allocation; operates on the existing view.
     */
    inline void feed_string(std::string_view text, StateMachine &machine)
    {
      for (const char c : text)
        machine.push(c);
    }

    /**
     * @brief Feed a std::istream into @p machine using a fixed stack buffer.
     *
     * Reads in kStreamBufSize-byte chunks until EOF.  Suitable for files of
     * arbitrary size without loading them fully into memory.
     *
     * @throws std::ios_base::failure on non-EOF stream errors.
     */
    inline void feed_stream(std::istream &input, StateMachine &machine)
    {
      char buf[kStreamBufSize];
      while (true)
      {
        input.read(buf, static_cast<std::streamsize>(kStreamBufSize));
        const auto n = static_cast<std::size_t>(input.gcount());
        for (std::size_t i = 0; i < n; ++i)
          machine.push(buf[i]);
        if (input.eof())
          break;
        if (!input.good())
          throw std::ios_base::failure("csv: I/O error reading stream");
      }
    }

  } // namespace detail

  /**
   * @brief Parse a CSV document from a std::string.
   *
   * The entire string is processed in a single pass without copying it.
   * For documents larger than available RAM prefer the istream overload.
   *
   * @param text  UTF-8 (or ASCII) CSV text.  May be empty.
   * @param opt   Parsing options; default = RFC 4180 / Python csv.reader.
   * @return      A Table (vector of Rows of strings).
   *
   * @throws csv::ParseError  on malformed CSV (unclosed quote, illegal byte
   *                          after closing quote, field/row size limit exceeded).
   *
   * @code
   *   csv::Table t = csv::parse("a,b,c\n1,2,3\n");
   *   // t[0] == {"a","b","c"}, t[1] == {"1","2","3"}
   * @endcode
   */
  [[nodiscard]] inline Table parse(const std::string &text,
                                   const Options &opt = {})
  {
    if (text.empty())
      return {};
    detail::StateMachine machine(opt);
    detail::feed_string(text, machine);
    machine.finish();
    return std::move(machine.table);
  }

  /**
   * @brief Parse a CSV document from a std::istream.
   *
   * Reads the stream from its current position to EOF in 64 KiB chunks.
   * No seek is performed; the stream is left at EOF after a successful parse.
   * Suitable for arbitrarily large files (stdin, network sockets, gzip streams).
   *
   * @param input  An open, readable std::istream (std::ifstream, std::cin, …).
   * @param opt    Parsing options.
   * @return       A Table.
   * @throws csv::ParseError         on malformed CSV.
   * @throws std::ios_base::failure  on non-EOF stream error.
   *
   * @code
   *   std::ifstream f("huge.csv");
   *   csv::Table t = csv::parse(f);
   * @endcode
   */
  [[nodiscard]] inline Table parse(std::istream &input,
                                   const Options &opt = {})
  {
    detail::StateMachine machine(opt);
    detail::feed_stream(input, machine);
    machine.finish();
    return std::move(machine.table);
  }

  /**
   * @brief Serialise a single Row to a CSV line without a trailing line ending.
   *
   * Fields that require quoting (contain separator, quote_char, CR, or LF) are
   * wrapped in quotes with internal quote_chars doubled per RFC 4180.
   *
   * @param row  Fields to serialise.
   * @param opt  Write options; default = comma separator, LF line ending.
   * @return     A std::string containing the serialised row.
   *
   * @code
   *   csv::Row r = {"hello", "world, earth", R"(say "hi")"};
   *   std::string line = csv::write_row(r);
   *   // -> hello,"world, earth","say ""hi"""
   * @endcode
   */
  [[nodiscard]] inline std::string write_row(const Row &row,
                                             const WriteOptions &opt = {})
  {
    std::string out;
    out.reserve(row.size() * 16);
    detail::serialise_row(row, opt, out);
    return out;
  }

  /**
   * @brief Serialise a full Table to a CSV string.
   *
   * Each row is serialised via write_row() and followed by opt.line_ending.
   * An empty table returns an empty string.
   *
   * @param table  Data to serialise.
   * @param opt    Write options.
   * @return       The complete CSV document as a std::string.
   *
   * @code
   *   csv::Table t = {{"name","age"},{"Alice","30"},{"Bob","25"}};
   *   std::string csv_text = csv::write(t);
   * @endcode
   */
  [[nodiscard]] inline std::string write(const Table &table,
                                         const WriteOptions &opt = {})
  {
    if (table.empty())
      return {};
    std::string out;
    if (!table.empty() && !table.front().empty())
      out.reserve(table.size() * table.front().size() * 16);
    for (const Row &row : table)
    {
      detail::serialise_row(row, opt, out);
      out += opt.line_ending;
    }
    return out;
  }

  /**
   * @brief Write a Table to a std::ostream row by row without building a
   *        complete in-memory string.
   *
   * Suitable for writing very large tables directly to files, sockets, or
   * compressed streams.
   *
   * @param output  Open, writable std::ostream.
   * @param table   Data to write.
   * @param opt     Write options.
   * @throws std::ios_base::failure if the stream enters a bad state.
   *
   * @code
   *   std::ofstream f("out.csv");
   *   csv::write_to(f, table);
   * @endcode
   */
  inline void write_to(std::ostream &output,
                       const Table &table,
                       const WriteOptions &opt = {})
  {
    std::string line;
    for (const Row &row : table)
    {
      line.clear();
      detail::serialise_row(row, opt, line);
      line += opt.line_ending;
      output.write(line.data(), static_cast<std::streamsize>(line.size()));
      if (!output.good())
        throw std::ios_base::failure("csv: I/O error writing stream");
    }
  }

  /**
   * @brief Open a file by path and parse its contents as CSV.
   *
   * Thin wrapper around std::ifstream + csv::parse(istream).  Opens the file
   * in binary mode to preserve CRLF bytes for the parser to handle.
   *
   * @param path  Null-terminated path to the CSV file.
   * @param opt   Parsing options.
   * @return      A Table.
   * @throws std::ios_base::failure  if the file cannot be opened.
   * @throws csv::ParseError         if the content is malformed.
   *
   * @code
   *   csv::Table employees = csv::load("employees.csv");
   * @endcode
   */
  [[nodiscard]] inline Table load(const char *path, const Options &opt = {})
  {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
      throw std::ios_base::failure(
          std::string("csv::load: cannot open '") + path + "'");
    return parse(f, opt);
  }

  /**
   * @brief std::string path overload of csv::load().
   *
   * @param path  Path to the CSV file.
   * @param opt   Parsing options.
   * @return      A Table.
   */
  [[nodiscard]] inline Table load(const std::string &path,
                                  const Options &opt = {})
  {
    return load(path.c_str(), opt);
  }

  /**
   * @brief Serialise @p table and write it to a file.
   *
   * Creates or overwrites the file at @p path.  Output is streamed via
   * csv::write_to() so no second in-memory copy is required.
   *
   * @param path   Null-terminated destination path.
   * @param table  Data to write.
   * @param opt    Write options.
   * @throws std::ios_base::failure on open or write error.
   *
   * @code
   *   csv::save("output.csv", table);
   * @endcode
   */
  inline void save(const char *path,
                   const Table &table,
                   const WriteOptions &opt = {})
  {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open())
      throw std::ios_base::failure(
          std::string("csv::save: cannot open '") + path + "'");
    write_to(f, table, opt);
    if (!f.good())
      throw std::ios_base::failure(
          std::string("csv::save: write error on '") + path + "'");
  }

  /**
   * @brief std::string path overload of csv::save().
   *
   * @param path   Destination file path.
   * @param table  Data to write.
   * @param opt    Write options.
   */
  inline void save(const std::string &path,
                   const Table &table,
                   const WriteOptions &opt = {})
  {
    save(path.c_str(), table, opt);
  }

  /**
   * @brief Read-only, non-owning view over a data Row paired with its header Row.
   *
   * Provides Python-DictReader-style named-column access without copying fields.
   * RowView stores two raw pointers and performs no heap allocation.
   *
   * @code
   *   csv::Table t = csv::parse("name,age\nAlice,30\n");
   *   csv::RowView v(t[0], t[1]);   // header = t[0], data = t[1]
   *   std::string_view name = v["name"];  // "Alice"
   *   std::string_view age  = v["age"];   // "30"
   * @endcode
   */
  class RowView
  {
  public:
    class iterator
    {
    public:
      using value_type = std::pair<std::string_view, std::string_view>;
      using difference_type = std::ptrdiff_t;
      using iterator_category = std::forward_iterator_tag;
      using pointer = const value_type *;
      using reference = value_type;

      iterator(const Row *h, const Row *d, std::size_t i)
          : h_(h), d_(d), i_(i) {}

      reference operator*() const { return {(*h_)[i_], (*d_)[i_]}; }
      iterator &operator++()
      {
        ++i_;
        return *this;
      }
      iterator operator++(int)
      {
        auto t = *this;
        ++i_;
        return t;
      }
      bool operator==(const iterator &o) const noexcept { return i_ == o.i_; }
      bool operator!=(const iterator &o) const noexcept { return i_ != o.i_; }

    private:
      const Row *h_;
      const Row *d_;
      std::size_t i_;
    };

    /**
     * @param header  The header row (column names).
     * @param data    The data row to view over.
     */
    RowView(const Row &header, const Row &data) noexcept
        : header_(&header), data_(&data)
    {
    }

    /// Number of viewable fields: min(header.size(), data.size()).
    [[nodiscard]] std::size_t size() const noexcept
    {
      return std::min(header_->size(), data_->size());
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    /// Access by 0-based column index.
    /// @throws std::out_of_range when @p idx >= size().
    [[nodiscard]] std::string_view at(std::size_t idx) const
    {
      if (idx >= size())
        throw std::out_of_range("csv::RowView::at: index out of range");
      return (*data_)[idx];
    }

    /// Access by column name (linear scan through header).
    /// @throws std::out_of_range when @p name is not in the header.
    [[nodiscard]] std::string_view operator[](std::string_view name) const
    {
      for (std::size_t i = 0; i < header_->size(); ++i)
      {
        if ((*header_)[i] == name)
        {
          if (i < data_->size())
            return (*data_)[i];
          throw std::out_of_range(
              "csv::RowView: column '" + std::string(name) +
              "' in header but data row is too short");
        }
      }
      throw std::out_of_range(
          "csv::RowView: column '" + std::string(name) + "' not found");
    }

    /// Return true if @p name exists anywhere in the header row.
    [[nodiscard]] bool contains(std::string_view name) const noexcept
    {
      return std::any_of(header_->begin(), header_->end(),
                         [name](const std::string &h)
                         { return h == name; });
    }

    /// Direct access to the underlying header Row.
    [[nodiscard]] const Row &header() const noexcept { return *header_; }

    /// Direct access to the underlying data Row.
    [[nodiscard]] const Row &data() const noexcept { return *data_; }

    iterator begin() const noexcept { return {header_, data_, 0}; }
    iterator end() const noexcept { return {header_, data_, size()}; }

  private:
    const Row *header_;
    const Row *data_;
  };

  /**
   * @brief Adapts a Table so that each data row is accessible by column name.
   *
   * The first row of the table is treated as the header.  All subsequent rows
   * are exposed as RowView objects through a forward iterator.
   *
   * Two construction modes are supported:
   *  - Owning: accepts `Table&&` — the DictReader owns the data.
   *  - Non-owning: accepts `const Table&` — the caller retains ownership and
   *    must ensure the Table outlives the DictReader.
   *
   * @code
   *   csv::Table t = csv::parse("name,age,city\nAlice,30,NYC\nBob,25,LA\n");
   *   csv::DictReader reader(std::move(t));
   *
   *   for (const csv::RowView& row : reader) {
   *       std::cout << row["name"] << " lives in " << row["city"] << '\n';
   *   }
   * @endcode
   */
  class DictReader
  {
  public:
    class iterator
    {
    public:
      using value_type = RowView;
      using difference_type = std::ptrdiff_t;
      using iterator_category = std::forward_iterator_tag;
      using pointer = const RowView *;
      using reference = RowView;

      iterator(const Row *header, const Row *data) noexcept
          : header_(header), data_(data)
      {
      }

      RowView operator*() const noexcept { return {*header_, *data_}; }
      iterator &operator++() noexcept
      {
        ++data_;
        return *this;
      }
      iterator operator++(int) noexcept
      {
        auto t = *this;
        ++data_;
        return t;
      }
      bool operator==(const iterator &o) const noexcept { return data_ == o.data_; }
      bool operator!=(const iterator &o) const noexcept { return data_ != o.data_; }

    private:
      const Row *header_;
      const Row *data_;
    };

    /// Owning constructor: takes ownership of @p t via move.
    /// Pass std::move(t) to invoke this overload.
    explicit DictReader(Table &&t)
        : owned_(std::move(t)), source_(&owned_)
    {
    }

    /// Non-owning constructor: borrows @p t (caller retains ownership).
    explicit DictReader(const Table &t) noexcept
        : owned_(), source_(&t)
    {
    }

    /// The header row (column names).  Empty when the table is empty.
    [[nodiscard]] const Row &fieldnames() const noexcept
    {
      static const Row empty;
      return source_->empty() ? empty : source_->front();
    }

    /// Number of data rows (header row excluded).
    [[nodiscard]] std::size_t size() const noexcept
    {
      return source_->size() > 1 ? source_->size() - 1 : 0;
    }

    [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    iterator begin() const noexcept
    {
      if (source_->size() < 2)
        return end();
      return {&(*source_)[0], &(*source_)[1]};
    }

    iterator end() const noexcept
    {
      if (source_->empty())
        return {nullptr, nullptr};
      return {&(*source_)[0], source_->data() + source_->size()};
    }

  private:
    Table owned_;         ///< Populated only in the owning constructor.
    const Table *source_; ///< Always points to the live table.
  };

  /**
   * @brief Writes CSV rows supplied as (name → value) associative containers.
   *
   * Column order is fixed by the header row given at construction time.  Missing
   * keys produce empty fields; extra keys are silently ignored.
   *
   * The output accumulates into an internal buffer.  Call str() to retrieve the
   * full CSV, release() to take ownership of the buffer, or flush_to() to stream
   * it to a std::ostream and reset.
   *
   * @code
   *   csv::DictWriter writer({"name","age","city"});
   *   writer.writerow({{"name","Alice"},{"age","30"},{"city","NYC"}});
   *   writer.writerow({{"name","Bob"},  {"age","25"}});   // city → ""
   *   std::string csv_text = writer.str();
   * @endcode
   */
  class DictWriter
  {
  public:
    /// A single row supplied as a flat list of name-value pairs.
    using Dict = std::vector<std::pair<std::string, std::string>>;

    /**
     * @param fieldnames    Ordered column names; becomes the first CSV row.
     * @param opt           Write options.
     * @param write_header  When true (default), the header row is written
     *                      immediately during construction.
     */
    explicit DictWriter(
        Row fieldnames,
        WriteOptions opt = {},
        bool write_header = true)
        : fields_(std::move(fieldnames)), opt_(std::move(opt))
    {
      if (write_header)
      {
        detail::serialise_row(fields_, opt_, buf_);
        buf_ += opt_.line_ending;
      }
    }

    /**
     * @brief Append one row.
     *
     * Fields are resolved in the order defined by fieldnames.  Keys absent
     * from @p d produce an empty field.  Extra keys are ignored.
     */
    void writerow(const Dict &d)
    {
      Row row;
      row.reserve(fields_.size());
      for (const std::string &name : fields_)
      {
        auto it = std::find_if(d.begin(), d.end(),
                               [&](const auto &kv)
                               { return kv.first == name; });
        row.push_back(it != d.end() ? it->second : std::string{});
      }
      detail::serialise_row(row, opt_, buf_);
      buf_ += opt_.line_ending;
    }

    /// Append multiple rows in order.
    void writerows(const std::vector<Dict> &rows)
    {
      for (const Dict &d : rows)
        writerow(d);
    }

    /// Return a const reference to the accumulated CSV output.
    [[nodiscard]] const std::string &str() const noexcept { return buf_; }

    /// Move the accumulated buffer out; leaves the writer in a valid empty
    /// state ready for further writes.
    [[nodiscard]] std::string release() noexcept
    {
      std::string tmp;
      std::swap(tmp, buf_);
      return tmp;
    }

    /// Write the buffer to @p output and clear it.
    void flush_to(std::ostream &output)
    {
      output.write(buf_.data(), static_cast<std::streamsize>(buf_.size()));
      buf_.clear();
    }

    /// Total bytes written to the internal buffer.
    [[nodiscard]] std::size_t bytes_written() const noexcept { return buf_.size(); }

  private:
    Row fields_;
    WriteOptions opt_;
    std::string buf_;
  };

  /**
   * @brief Callback-based CSV parser that never builds a Table in memory.
   *
   * For each committed row the user-supplied callback is invoked.  The parser
   * can be driven incrementally (byte buffers via push()) or given a complete
   * source all at once (parse() overloads).
   *
   * @code
   *   std::size_t row_count = 0;
   *   csv::StreamingParser p([&](const csv::Row& row){
   *       ++row_count;
   *       // Process row without storing it
   *   });
   *
   *   std::ifstream f("huge.csv");
   *   p.parse(f);
   *   std::cout << row_count << " rows\n";
   * @endcode
   */
  class StreamingParser
  {
  public:
    /// The row callback type.
    using Callback = std::function<void(const Row &)>;

    /**
     * @param callback  Invoked once for every committed row.
     * @param opt       Parsing options.  All filters (skip_empty_lines,
     *                  skip_comments, row_filter, etc.) are honoured before
     *                  the callback is called.
     */
    explicit StreamingParser(Callback callback, Options opt = {})
        : callback_(std::move(callback)), opt_(std::move(opt))
    {
      install_callback_hook();
      reset_machine();
    }

    /// Push a single byte.
    void push(char c) { machine_->push(c); }

    /// Push @p n bytes from @p data.
    void push(const char *data, std::size_t n)
    {
      for (std::size_t i = 0; i < n; ++i)
        machine_->push(data[i]);
    }

    /// Signal end-of-input.
    void finish() { machine_->finish(); }

    /// Parse a complete std::string_view.
    void parse(std::string_view text)
    {
      detail::feed_string(text, *machine_);
      machine_->finish();
    }

    /// Parse from an open std::istream.
    void parse(std::istream &input)
    {
      detail::feed_stream(input, *machine_);
      machine_->finish();
    }

    /// Reset the parser so it can be reused for a new document.
    void reset() { reset_machine(); }

  private:
    /// Install a row_filter hook that fires the callback and drops every row
    /// from the internal table (so no memory is consumed).
    void install_callback_hook()
    {
      auto cb = callback_;                 // copy into lambda
      auto outer_filter = opt_.row_filter; // preserve existing filter
      opt_.row_filter = [cb, outer_filter](const Row &row) -> bool
      {
        if (outer_filter && !outer_filter(row))
          return false;
        cb(row);
        return false; // never accumulate
      };
    }

    void reset_machine()
    {
      machine_ = std::make_unique<detail::StateMachine>(opt_);
    }

    Callback callback_;
    Options opt_;
    std::unique_ptr<detail::StateMachine> machine_;
  };

  namespace detail
  {

    /**
     * @brief A lightweight non-owning C++20 forward range that presents the data
     *        rows of a Table (header at index 0 skipped) as RowView objects.
     *
     * Intended to be used through the csv::rows() factory function.
     */
    class TableRange
    {
    public:
      class iterator
      {
      public:
        using value_type = RowView;
        using difference_type = std::ptrdiff_t;
        using iterator_category = std::forward_iterator_tag;
        using pointer = const RowView *;
        using reference = RowView;

        iterator(const Row *header, const Row *data) noexcept
            : header_(header), data_(data) {}

        RowView operator*() const noexcept { return {*header_, *data_}; }
        iterator &operator++() noexcept
        {
          ++data_;
          return *this;
        }
        iterator operator++(int) noexcept
        {
          auto t = *this;
          ++data_;
          return t;
        }
        bool operator==(const iterator &o) const noexcept { return data_ == o.data_; }
        bool operator!=(const iterator &o) const noexcept { return data_ != o.data_; }

      private:
        const Row *header_;
        const Row *data_;
      };

      explicit TableRange(const Table &t) noexcept : table_(&t) {}

      iterator begin() const noexcept
      {
        if (table_->size() < 2)
          return end();
        return {&(*table_)[0], &(*table_)[1]};
      }

      iterator end() const noexcept
      {
        if (table_->empty())
          return {nullptr, nullptr};
        return {&(*table_)[0], table_->data() + table_->size()};
      }

      [[nodiscard]] std::size_t size() const noexcept
      {
        return table_->size() > 1 ? table_->size() - 1 : 0;
      }
      [[nodiscard]] bool empty() const noexcept { return size() == 0; }

    private:
      const Table *table_;
    };

  } // namespace detail

  /**
   * @brief Return a forward range of RowView objects over the *data* rows of
   *        @p table.  The header row (index 0) is skipped automatically.
   *
   * The returned range satisfies std::ranges::forward_range (C++20) and is
   * compatible with range-for loops and standard range algorithms.
   *
   * @note @p table must outlive the returned range object.
   *
   * @code
   *   for (const csv::RowView& row : csv::rows(table))
   *       std::cout << row["name"] << '\n';
   * @endcode
   */
  [[nodiscard]] inline detail::TableRange rows(const Table &table) noexcept
  {
    return detail::TableRange(table);
  }

  /**
   * @brief Return the 0-based index of column @p name in @p header, or
   *        std::nullopt if not found.
   *
   * @code
   *   auto idx = csv::column_index(table[0], "age");
   *   if (idx) std::cout << "age is column " << *idx << '\n';
   * @endcode
   */
  [[nodiscard]] inline std::optional<std::size_t>
  column_index(const Row &header, std::string_view name) noexcept
  {
    for (std::size_t i = 0; i < header.size(); ++i)
      if (header[i] == name)
        return i;
    return std::nullopt;
  }

  /**
   * @brief Extract a single named column from @p table as a vector of strings.
   *
   * The first row is treated as the header; it is excluded from the returned
   * vector.  Rows where the target column index is out-of-range contribute an
   * empty string.
   *
   * @param table  Source table with a header row.
   * @param name   Column name (must appear in the first row).
   * @return       One string per data row.
   * @throws std::out_of_range if @p name is absent from the header.
   *
   * @code
   *   auto ages = csv::column(table, "age");   // std::vector<std::string>
   * @endcode
   */
  [[nodiscard]] inline std::vector<std::string>
  column(const Table &table, std::string_view name)
  {
    if (table.empty())
      return {};
    const auto idx = column_index(table.front(), name);
    if (!idx)
      throw std::out_of_range(
          "csv::column: column '" + std::string(name) + "' not found");
    std::vector<std::string> result;
    result.reserve(table.size() > 0 ? table.size() - 1 : 0);
    for (std::size_t r = 1; r < table.size(); ++r)
    {
      const Row &row = table[r];
      result.push_back(*idx < row.size() ? row[*idx] : std::string{});
    }
    return result;
  }

  /**
   * @brief Return a new Table containing only the specified columns, in the
   *        given order.
   *
   * The header row is always included as the first row of the result.
   *
   * @param table    Source table (first row = header).
   * @param columns  Column names to include; duplicates are allowed.
   * @return         New Table with selected columns.
   * @throws std::out_of_range if any requested column is not in the header.
   *
   * @code
   *   auto sub = csv::select_columns(table, {"name", "city"});
   * @endcode
   */
  [[nodiscard]] inline Table
  select_columns(const Table &table,
                 const std::vector<std::string> &columns)
  {
    if (table.empty())
      return {};
    const Row &header = table.front();

    std::vector<std::size_t> indices;
    indices.reserve(columns.size());
    for (const std::string &name : columns)
    {
      const auto idx = column_index(header, name);
      if (!idx)
        throw std::out_of_range(
            "csv::select_columns: column '" + name + "' not found");
      indices.push_back(*idx);
    }

    Table result;
    result.reserve(table.size());
    for (const Row &src : table)
    {
      Row dst;
      dst.reserve(indices.size());
      for (const std::size_t i : indices)
        dst.push_back(i < src.size() ? src[i] : std::string{});
      result.push_back(std::move(dst));
    }
    return result;
  }

  /**
   * @brief Return a new Table keeping only the rows for which @p pred returns
   *        true.  The header row (index 0) is always preserved.
   *
   * @param table  Source table.
   * @param pred   Predicate: const Row& → bool.
   * @return       Filtered table.
   *
   * @code
   *   auto young = csv::filter_rows(table, [](const csv::Row& r){
   *       return r.size() >= 2 && r[1] < "30";
   *   });
   * @endcode
   */
  template <RowPredicate Pred>
  [[nodiscard]] inline Table filter_rows(const Table &table, Pred &&pred)
  {
    if (table.empty())
      return {};
    Table result;
    result.reserve(table.size());
    result.push_back(table.front()); // always keep header
    for (std::size_t r = 1; r < table.size(); ++r)
      if (std::forward<Pred>(pred)(table[r]))
        result.push_back(table[r]);
    return result;
  }

  /**
   * @brief Apply @p xform to every field in a *copy* of @p table and return
   *        the transformed copy.
   *
   * Both header and data rows are transformed.
   *
   * @param table  Source table.
   * @param xform  Callable: std::string& → void.
   * @return       Transformed copy.
   *
   * @code
   *   auto clean = csv::transform_fields(table, [](std::string& s){
   *       // Strip all whitespace in-place
   *       s.erase(std::remove_if(s.begin(), s.end(), ::isspace), s.end());
   *   });
   * @endcode
   */
  template <FieldTransformer Xform>
  [[nodiscard]] inline Table transform_fields(Table table, Xform &&xform)
  {
    for (Row &row : table)
      for (std::string &field : row)
        std::forward<Xform>(xform)(field);
    return table;
  }

  /**
   * @brief Transpose @p table (rows become columns).
   *
   * Unequal row lengths are handled by padding with empty strings so that every
   * output row is of uniform length.
   *
   * @code
   *   // {{"a","b"},{"1","2"},{"3","4"}} → {{"a","1","3"},{"b","2","4"}}
   * @endcode
   */
  [[nodiscard]] inline Table transpose(const Table &table)
  {
    if (table.empty())
      return {};
    const std::size_t cols =
        std::max_element(table.begin(), table.end(),
                         [](const Row &a, const Row &b)
                         {
                           return a.size() < b.size();
                         })
            ->size();
    Table result(cols, Row(table.size()));
    for (std::size_t r = 0; r < table.size(); ++r)
      for (std::size_t c = 0; c < table[r].size(); ++c)
        result[c][r] = table[r][c];
    return result;
  }

  /**
   * @brief Vertically concatenate two Tables (append rows of @p b onto @p a).
   *
   * No header deduplication is performed.  Use vstack_skip_header() to strip
   * the header of @p b before appending.
   */
  [[nodiscard]] inline Table vstack(Table a, const Table &b)
  {
    a.reserve(a.size() + b.size());
    for (const Row &r : b)
      a.push_back(r);
    return a;
  }

  /**
   * @brief Concatenate two Tables, discarding the header row of @p b.
   *
   * Useful when merging multiple CSV files that each carry a header.
   * The header of @p a is preserved; rows 1..N of @p b are appended.
   */
  [[nodiscard]] inline Table vstack_skip_header(Table a, const Table &b)
  {
    if (b.size() < 2)
      return a;
    a.reserve(a.size() + b.size() - 1);
    for (std::size_t r = 1; r < b.size(); ++r)
      a.push_back(b[r]);
    return a;
  }

  /**
   * @brief Return true when two Tables are identical in shape and content.
   *
   * Simple wrapper around operator== included for clarity in assertion code.
   */
  [[nodiscard]] inline bool tables_equal(const Table &a,
                                         const Table &b) noexcept
  {
    return a == b;
  }

  /**
   * @brief Result of dialect sniffing: best-guess format parameters.
   */
  struct Dialect
  {
    char separator = ',';           ///< Detected field separator.
    char quote_char = '"';          ///< Detected quote character.
    bool has_header = false;        ///< True when the first row looks like a header.
    std::string line_ending = "\n"; ///< Detected line ending ("\n" or "\r\n").
  };

  /**
   * @brief Heuristically infer the CSV dialect from a representative text sample.
   *
   * Algorithm:
   *  1. Count CRLF vs bare LF to choose the line ending.
   *  2. Try each candidate separator (`,` `;` `\t` `|`), parse the sample
   *     for each, and pick the one that produces the most rows with a
   *     consistent column count.
   *  3. Detect a header by comparing whether the first row contains numeric
   *     fields while later rows do.  First row has no numbers + later rows do
   *     → likely a header.
   *
   * @param sample  A prefix of the CSV content (4–64 KiB recommended).
   * @return        A Dialect with the best-guess parameters.
   *
   * @code
   *   auto d = csv::sniff(first_64k);
   *   csv::Options opt;
   *   opt.separator = d.separator;
   *   csv::Table t  = csv::parse(full_text, opt);
   * @endcode
   */
  [[nodiscard]] inline Dialect sniff(std::string_view sample)
  {
    Dialect result;

    {
      std::size_t crlf = 0, lf = 0;
      for (std::size_t i = 0; i < sample.size(); ++i)
      {
        if (sample[i] == '\r' &&
            i + 1 < sample.size() && sample[i + 1] == '\n')
        {
          ++crlf;
          ++i;
        }
        else if (sample[i] == '\n')
        {
          ++lf;
        }
      }
      result.line_ending = (crlf > lf) ? "\r\n" : "\n";
    }

    {
      const char candidates[] = {',', ';', '\t', '|'};
      std::size_t best_score = 0;
      char best_sep = ',';

      for (const char sep : candidates)
      {
        Options opt;
        opt.separator = sep;
        opt.skip_empty_lines = true;
        try
        {
          const Table t = parse(std::string(sample), opt);
          if (t.empty())
            continue;
          const std::size_t expected = t.front().size();
          if (expected < 2)
            continue; // single column is uninteresting
          std::size_t matches = 0;
          for (const Row &r : t)
            if (r.size() == expected)
              ++matches;
          const std::size_t score = matches * expected;
          if (score > best_score)
          {
            best_score = score;
            best_sep = sep;
          }
        }
        catch (...)
        {
        }
      }
      result.separator = best_sep;
    }

    {
      auto looks_numeric = [](const std::string &s) -> bool
      {
        if (s.empty())
          return false;
        bool digit = false;
        std::size_t i = 0;
        if (s[i] == '-' || s[i] == '+')
          ++i;
        for (; i < s.size(); ++i)
        {
          const char c = s[i];
          if (c >= '0' && c <= '9')
          {
            digit = true;
            continue;
          }
          if (c == '.' || c == 'e' || c == 'E' ||
              c == '+' || c == '-')
            continue;
          return false;
        }
        return digit;
      };

      Options opt;
      opt.separator = result.separator;
      opt.skip_empty_lines = true;
      try
      {
        const Table t = parse(std::string(sample), opt);
        if (t.size() >= 2)
        {
          std::size_t num_first = 0;
          for (const std::string &f : t[0])
            if (looks_numeric(f))
              ++num_first;

          std::size_t num_rest = 0;
          const std::size_t cap = std::min(t.size(), std::size_t(6));
          for (std::size_t r = 1; r < cap; ++r)
            for (const std::string &f : t[r])
              if (looks_numeric(f))
                ++num_rest;

          result.has_header = (num_first == 0 && num_rest > 0);
        }
      }
      catch (...)
      {
      }
    }

    return result;
  }

  /**
   * @brief Return a human-readable summary of @p table suitable for debugging.
   *
   * Outputs the row count, column count, and a preview of the first few rows
   * with fields quoted for unambiguous display.
   *
   * @param table     The table to describe.
   * @param max_rows  Maximum number of rows (header + data) to display.
   * @return          A multi-line std::string.
   *
   * @code
   *   std::cout << csv::describe(table) << '\n';
   * @endcode
   */
  [[nodiscard]] inline std::string describe(
      const Table &table,
      std::size_t max_rows = 5)
  {
    std::ostringstream oss;
    oss << "csv::Table: " << table.size() << " row(s)";
    if (!table.empty())
      oss << ", " << table.front().size() << " column(s)";
    oss << '\n';

    const std::size_t limit = std::min(table.size(), max_rows + 1);
    for (std::size_t r = 0; r < limit; ++r)
    {
      if (r == 0)
        oss << "  [header]  ";
      else
        oss << "  [" << std::to_string(r) << "]       ";
      for (std::size_t c = 0; c < table[r].size(); ++c)
      {
        if (c)
          oss << " | ";
        oss << '"' << table[r][c] << '"';
      }
      oss << '\n';
    }
    if (table.size() > limit)
      oss << "  ... (" << (table.size() - limit)
          << " more rows not shown)\n";
    return oss.str();
  }

  /**
   * @brief Return the library version as a "major.minor.patch" string.
   *
   * @code
   *   std::cout << "csv.hpp v" << csv::version() << '\n';
   * @endcode
   */
  [[nodiscard]] inline std::string version()
  {
    return std::to_string(RIXCPP_CSV_VERSION_MAJOR) + '.' +
           std::to_string(RIXCPP_CSV_VERSION_MINOR) + '.' +
           std::to_string(RIXCPP_CSV_VERSION_PATCH);
  }

  /// @deprecated Renamed to csv::load().  Will be removed in v3.0.
  [[deprecated("Use csv::load() instead")]] [[nodiscard]] inline Table load_csv(
      const std::string &path,
      const Options &opt = {})
  {
    return load(path, opt);
  }

  /// @deprecated Renamed to csv::save().  Will be removed in v3.0.
  [[deprecated("Use csv::save() instead")]]
  inline void save_csv(const std::string &path,
                       const Table &table,
                       const WriteOptions &opt = {})
  {
    save(path, table, opt);
  }

} // namespace rixlib::csv

#endif // RIXCPP_CSV_INCLUDE_RIX_CSV_HPP_INCLUDED
