add_executable(rix_csv_tests
  csv_tests.cpp
)

target_link_libraries(rix_csv_tests
  PRIVATE
    rix::csv
)

add_test(
  NAME rix_csv_tests
  COMMAND rix_csv_tests
)
