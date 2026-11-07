# Agentic-OpenSCAD: MCP server sources.
#
# Included from the top-level CMakeLists.txt when ENABLE_MCP is ON. Appends to
# CORE_SOURCES so the code is compiled into OpenSCADLibInternal in both the GUI
# and the HEADLESS build. Keep all MCP source additions in this file so the
# top-level CMakeLists.txt carries a single hook line (see doc/agentic/upstream-merge.md).

list(APPEND CORE_SOURCES
  src/mcp/BuildInfo.cc
  src/mcp/docs/BuiltinDocs.cc
  src/mcp/docs/Docs.cc
  src/mcp/docs/Html.cc
  src/mcp/docs/SearchIndex.cc
  src/mcp/engine/Base64.cc
  src/mcp/engine/CompileEngine.cc
  src/mcp/engine/ParamEngine.cc
  src/mcp/engine/ViewEngine.cc
  src/mcp/headless/HeadlessMain.cc
  src/mcp/headless/HeadlessSession.cc
  src/mcp/library/Archive.cc
  src/mcp/library/BundledRegistry.cc
  src/mcp/library/Download.cc
  src/mcp/library/LibraryManager.cc
  src/mcp/library/Process.cc
  src/mcp/library/Registry.cc
  src/mcp/protocol/JsonRpc.cc
  src/mcp/protocol/Registry.cc
  src/mcp/protocol/Schema.cc
  src/mcp/protocol/Server.cc
  src/mcp/session/Diff.cc
  src/mcp/session/FileStore.cc
  src/mcp/session/MessageLog.cc
  src/mcp/session/Project.cc
  src/mcp/session/ProjectSession.cc
  src/mcp/session/SessionManager.cc
  src/mcp/tools/CompileTools.cc
  src/mcp/tools/CoreTools.cc
  src/mcp/tools/DocsTools.cc
  src/mcp/tools/FileTools.cc
  src/mcp/tools/LibraryTools.cc
  src/mcp/tools/ParamTools.cc
  src/mcp/tools/ViewTools.cc
  src/mcp/transport/HttpTransport.cc
  src/mcp/transport/StdioTransport.cc
)

if(NOT HEADLESS)
  list(APPEND CORE_SOURCES
    src/mcp/viewer/ViewerApp.cc
    src/mcp/viewer/ViewerBridge.cc
    src/mcp/viewer/ViewerWindow.cc
  )
endif()

target_compile_definitions(OpenSCADLibInternal PUBLIC ENABLE_MCP)
