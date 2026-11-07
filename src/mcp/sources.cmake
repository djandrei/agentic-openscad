# Agentic-OpenSCAD: MCP server sources.
#
# Included from the top-level CMakeLists.txt when ENABLE_MCP is ON. Appends to
# CORE_SOURCES so the code is compiled into OpenSCADLibInternal in both the GUI
# and the HEADLESS build. Keep all MCP source additions in this file so the
# top-level CMakeLists.txt carries a single hook line (see doc/agentic/upstream-merge.md).

list(APPEND CORE_SOURCES
  src/mcp/BuildInfo.cc
)

target_compile_definitions(OpenSCADLibInternal PUBLIC ENABLE_MCP)
