# CMake generated Testfile for 
# Source directory: /usr/team_project/mcp-tutorial/tests
# Build directory: /usr/team_project/mcp-tutorial/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
include("/usr/team_project/mcp-tutorial/build/tests/test_logger[1]_include.cmake")
include("/usr/team_project/mcp-tutorial/build/tests/test_config[1]_include.cmake")
include("/usr/team_project/mcp-tutorial/build/tests/test_json_rpc[1]_include.cmake")
add_test(LoggerTest "/usr/team_project/mcp-tutorial/build/tests/test_logger")
set_tests_properties(LoggerTest PROPERTIES  _BACKTRACE_TRIPLES "/usr/team_project/mcp-tutorial/tests/CMakeLists.txt;28;add_test;/usr/team_project/mcp-tutorial/tests/CMakeLists.txt;0;")
add_test(ConfigTest "/usr/team_project/mcp-tutorial/build/tests/test_config")
set_tests_properties(ConfigTest PROPERTIES  _BACKTRACE_TRIPLES "/usr/team_project/mcp-tutorial/tests/CMakeLists.txt;29;add_test;/usr/team_project/mcp-tutorial/tests/CMakeLists.txt;0;")
add_test(JsonRpcTest "/usr/team_project/mcp-tutorial/build/tests/test_json_rpc")
set_tests_properties(JsonRpcTest PROPERTIES  _BACKTRACE_TRIPLES "/usr/team_project/mcp-tutorial/tests/CMakeLists.txt;49;add_test;/usr/team_project/mcp-tutorial/tests/CMakeLists.txt;0;")
