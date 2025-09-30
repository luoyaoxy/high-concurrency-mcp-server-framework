# CMake generated Testfile for 
# Source directory: /usr/team_project/mcp-tutorial/tests
# Build directory: /usr/team_project/mcp-tutorial/build/tests
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
include("/usr/team_project/mcp-tutorial/build/tests/test_json_rpc[1]_include.cmake")
include("/usr/team_project/mcp-tutorial/build/tests/test_mcp_client[1]_include.cmake")
include("/usr/team_project/mcp-tutorial/build/tests/test_example[1]_include.cmake")
add_test(JsonRpcTest "/usr/team_project/mcp-tutorial/build/tests/test_json_rpc")
set_tests_properties(JsonRpcTest PROPERTIES  _BACKTRACE_TRIPLES "/usr/team_project/mcp-tutorial/tests/CMakeLists.txt;24;add_test;/usr/team_project/mcp-tutorial/tests/CMakeLists.txt;0;")
add_test(McpClientTest "/usr/team_project/mcp-tutorial/build/tests/test_mcp_client")
set_tests_properties(McpClientTest PROPERTIES  _BACKTRACE_TRIPLES "/usr/team_project/mcp-tutorial/tests/CMakeLists.txt;25;add_test;/usr/team_project/mcp-tutorial/tests/CMakeLists.txt;0;")
