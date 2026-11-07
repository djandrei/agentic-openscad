# Agentic-OpenSCAD end-to-end tests: a standard-library Python MCP client runs
# the full agent scenario (project, files, compile feedback, captures,
# customizer, libraries, docs) against the built binary.

find_package(Python3 3.4 COMPONENTS Interpreter REQUIRED QUIET)

set(AGENTIC_E2E "${CMAKE_CURRENT_SOURCE_DIR}/e2e_client.py")
# Keep the user's documentation cache untouched and avoid network access.
set(AGENTIC_TEST_ENV "AGENTIC_OPENSCAD_DOCS_CACHE=${CMAKE_CURRENT_BINARY_DIR}/docs-cache")

add_test(NAME agentic-e2e-stdio
  CONFIGURATIONS Default
  COMMAND ${Python3_EXECUTABLE} ${AGENTIC_E2E} --stdio $<TARGET_FILE:OpenSCADExe> --expect-mode headless
)
set_tests_properties(agentic-e2e-stdio PROPERTIES ENVIRONMENT "${AGENTIC_TEST_ENV}" TIMEOUT 600)

add_test(NAME agentic-e2e-http-headless
  CONFIGURATIONS Default
  COMMAND ${Python3_EXECUTABLE} ${AGENTIC_E2E} --launch $<TARGET_FILE:OpenSCADExe> --launch-mode headless
)
set_tests_properties(agentic-e2e-http-headless PROPERTIES ENVIRONMENT "${AGENTIC_TEST_ENV}" TIMEOUT 600)

if(NOT HEADLESS)
  find_program(XVFB_RUN xvfb-run)
  if(XVFB_RUN)
    add_test(NAME agentic-e2e-gui
      CONFIGURATIONS Default
      COMMAND ${XVFB_RUN} -a -s "-screen 0 1280x1024x24"
              ${Python3_EXECUTABLE} ${AGENTIC_E2E} --launch $<TARGET_FILE:OpenSCADExe> --launch-mode gui
    )
    set_tests_properties(agentic-e2e-gui PROPERTIES ENVIRONMENT "${AGENTIC_TEST_ENV}" TIMEOUT 600)
  else()
    message(STATUS "xvfb-run not found: the agentic-e2e-gui test is not registered")
  endif()
endif()
