add_executable(rix_csv_basic
  basic.cpp
)

target_link_libraries(rix_csv_basic
  PRIVATE
    rix::csv
)
