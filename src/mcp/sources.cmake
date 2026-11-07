# Agentic-OpenSCAD: MCP server sources.
#
# Included from the top-level CMakeLists.txt when ENABLE_MCP is ON. Appends to
# CORE_SOURCES so the code is compiled into OpenSCADLibInternal in both the GUI
# and the HEADLESS build. Keep all MCP source additions in this file so the
# top-level CMakeLists.txt carries a single hook line (see doc/agentic/upstream-merge.md).

list(APPEND CORE_SOURCES
  src/mcp/BuildInfo.cc
  src/mcp/headless/HeadlessMain.cc
  src/mcp/headless/HeadlessSession.cc
  src/mcp/protocol/JsonRpc.cc
  src/mcp/protocol/Registry.cc
  src/mcp/protocol/Schema.cc
  src/mcp/protocol/Server.cc
  src/mcp/session/Project.cc
  src/mcp/session/SessionManager.cc
  src/mcp/tools/CoreTools.cc
  src/mcp/transport/HttpTransport.cc
  src/mcp/transport/StdioTransport.cc
)

if(NOT HEADLESS)
  list(APPEND CORE_SOURCES
    src/mcp/gui/AgentBridge.cc
    src/mcp/gui/AgentStatusWidget.cc
    src/mcp/gui/GuiSession.cc
  )
endif()

target_compile_definitions(OpenSCADLibInternal PUBLIC ENABLE_MCP)
