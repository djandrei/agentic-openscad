#include "mcp/viewer/ViewerApp.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QString>
#include <QSurfaceFormat>

#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "core/parsersettings.h"
#include "gui/QSettingsCached.h"
#include "mcp/viewer/ViewerBridge.h"
#include "mcp/viewer/ViewerWindow.h"
#include "openscad.h"
#include "version.h"

namespace mcp {

namespace {

// Same context setup as the classic GUI: OpenSCAD's renderers need a
// desktop OpenGL compatibility profile with depth and stencil buffers.
void configureOpenGLContext()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
  if (qEnvironmentVariableIsEmpty("QT_OPENGL")) {
    QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);
  }
  auto format = QSurfaceFormat::defaultFormat();
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setProfile(QSurfaceFormat::CompatibilityProfile);
  if (format.depthBufferSize() < 24) format.setDepthBufferSize(24);
  if (format.stencilBufferSize() < 8) format.setStencilBufferSize(8);
  QSurfaceFormat::setDefaultFormat(format);
#endif
}

}  // namespace

int runViewerApp(int argc, char **argv, const Options& options,
                 const std::vector<std::string>& inputFiles)
{
  configureOpenGLContext();
  QApplication app(argc, argv);

  // Share the settings store with the classic GUI (agent/port, color scheme, fonts).
  QCoreApplication::setOrganizationName("OpenSCAD");
  QCoreApplication::setOrganizationDomain("openscad.org");
  QCoreApplication::setApplicationName("OpenSCAD");
  QCoreApplication::setApplicationVersion(QString::fromStdString(std::string(openscad_versionnumber)));
  QGuiApplication::setApplicationDisplayName("Agentic OpenSCAD");
  app.setWindowIcon(QIcon(":/logo.png"));

  parser_init();
  const QSettingsCached settings;
  if (settings.value("advanced/localization", true).toBool()) localization_init();

  ViewerWindow window;
  ViewerBridge bridge(options, &window);
  window.show();

  std::string error;
  if (!bridge.start(&error)) {
    std::cerr << "agentic-openscad: " << error << "\n";
    return 1;
  }

  // --project, or the folder of the first file given on the command line.
  std::string project = options.project;
  if (project.empty() && !inputFiles.empty() && !inputFiles.front().empty()) {
    std::error_code ec;
    const auto first = std::filesystem::absolute(inputFiles.front(), ec);
    if (!ec) project = first.parent_path().string();
  }
  if (!project.empty()) {
    try {
      bridge.sessions().open(project);
    } catch (const std::exception& e) {
      std::cerr << "agentic-openscad: " << e.what() << "\n";
    }
  }

  return app.exec();
}

}  // namespace mcp
