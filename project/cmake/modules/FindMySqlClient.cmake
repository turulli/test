
find_path(MYSQLCLIENT_INCLUDE_DIRS mysql_time.h PATH_SUFFIXES mysql)
find_library(MYSQLCLIENT_LIBRARIES NAMES mysqlclient mysqlclient_r)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MySqlClient DEFAULT_MSG MYSQLCLIENT_LIBRARIES MYSQLCLIENT_INCLUDE_DIRS)
mark_as_advanced(MYSQLCLIENT_LIBRARIES MYSQLCLIENT_INCLUDE_DIRS)
list(APPEND MYSQLCLIENT_DEFINTIIONS -DHAVE_MYSQL=1)
