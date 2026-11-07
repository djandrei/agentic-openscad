#include "mcp/viewer/ViewerWindow.h"

#include <QLabel>
#include <QStatusBar>

#include <memory>
#include <string>

#include "core/Tree.h"
#include "geometry/Geometry.h"
#include "geometry/PolySet.h"
#include "geometry/Polygon2d.h"
#include "glview/CsgInfo.h"
#include "glview/PolySetRenderer.h"
#include "glview/RenderSettings.h"
#include "glview/Renderer.h"
#include "glview/preview/ThrownTogetherRenderer.h"
#include "gui/QGLView.h"
#include "mcp/session/EngineSession.h"
#ifdef ENABLE_OPENCSG
#include "glview/preview/OpenCSGRenderer.h"
#endif
#ifdef ENABLE_CGAL
#include "glview/cgal/CGALRenderer.h"
#endif

namespace mcp {

ViewerWindow::ViewerWindow()
{
  setWindowTitle(tr("Agentic OpenSCAD"));
  resize(1200, 800);

  view_ = new QGLView(this);
  setCentralWidget(view_);

  statusLabel_ = new QLabel(tr("Waiting for the agent…"), this);
  serverLabel_ = new QLabel(this);
  statusBar()->addWidget(statusLabel_, 1);
  statusBar()->addPermanentWidget(serverLabel_);
}

void ViewerWindow::setServerStatus(const QString& text)
{
  serverLabel_->setText(text);
}

void ViewerWindow::showSession(EngineSession *session)
{
  if (session_ == session) return;
  if (session_) session_->setObserver(nullptr);
  session_ = session;
  if (session_) session_->setObserver(this);
  lastCompile_.clear();
  refreshScene();
}

std::shared_ptr<Renderer> ViewerWindow::buildRenderer()
{
  if (!session_) return nullptr;
  const EngineResult& result = session_->lastResult();
  const std::string& viewMode = session_->viewMode();
  const bool wantRender = viewMode == "render" ||
                          (session_->lastMode() == CompileMode::Render && viewMode != "thrownTogether");
  if (wantRender && result.geometry) {
#if defined(ENABLE_CGAL) && !defined(USE_POLYSET_FOR_CGAL)
    if (RenderSettings::inst()->backend3D != RenderBackend3D::ManifoldBackend &&
        !std::dynamic_pointer_cast<const PolySet>(result.geometry) &&
        !std::dynamic_pointer_cast<const Polygon2d>(result.geometry)) {
      return std::make_shared<CGALRenderer>(result.geometry);
    }
#endif
    return std::make_shared<PolySetRenderer>(result.geometry);
  }
  if (!result.rootNode) return nullptr;

  Tree tree(result.rootNode, result.documentPath);
  CsgInfo info;
  info.compile_products(tree);
#ifdef ENABLE_OPENCSG
  if (viewMode != "thrownTogether" && view_->hasOpenCSGSupport()) {
    return std::make_shared<OpenCSGRenderer>(info.root_products, info.highlights_products,
                                             info.background_products);
  }
#endif
  return std::make_shared<ThrownTogetherRenderer>(info.root_products, info.highlights_products,
                                                  info.background_products);
}

void ViewerWindow::refreshScene()
{
  renderer_ = buildRenderer();
  view_->setRenderer(renderer_);
  applyViewOptions();
  applyCamera();
  updateStatus();
}

void ViewerWindow::applyCamera()
{
  if (!session_) return;
  // The agent's camera is the reference: any mouse orbiting is replaced.
  view_->cam = session_->camera();
  if (view_->cam.viewall) view_->viewAll();
  view_->update();
}

void ViewerWindow::applyViewOptions()
{
  if (!session_) return;
  const ViewOptions& options = session_->viewOptions();
  view_->setShowEdges(options.flags.at("edges"));
  view_->setShowAxes(options.flags.at("axes"));
  view_->setShowCrosshairs(options.flags.at("crosshairs"));
  view_->setShowScaleProportional(options.flags.at("scales"));
  view_->setColorScheme(session_->colorScheme());
  view_->update();
}

void ViewerWindow::updateStatus()
{
  if (!session_) {
    statusLabel_->setText(tr("Waiting for the agent…"));
    setWindowTitle(tr("Agentic OpenSCAD"));
    return;
  }
  const QString project = QString::fromStdString(session_->project().name());
  setWindowTitle(tr("%1 – Agentic OpenSCAD").arg(project));
  QString text = project;
  if (!session_->lastFile().empty()) text += "  ·  " + QString::fromStdString(session_->lastFile());
  if (!lastCompile_.isEmpty()) text += "  ·  " + lastCompile_;
  if (session_->paused()) text += "  ·  " + tr("PAUSED");
  statusLabel_->setText(text);
}

void ViewerWindow::compileFinished(EngineSession& session, const CompileRequest& request,
                                   const CompileOutcome& outcome)
{
  if (&session != session_) return;
  const char *mode = request.mode == CompileMode::Check     ? "check"
                     : request.mode == CompileMode::Preview ? "preview"
                                                            : "render";
  lastCompile_ = tr("%1 %2: %3 errors, %4 warnings, %5 ms")
                   .arg(QString::fromUtf8(mode), outcome.ok ? tr("ok") : tr("FAILED"))
                   .arg(outcome.errors)
                   .arg(outcome.warnings)
                   .arg(static_cast<int>(outcome.millis));
  if (request.mode == CompileMode::Check) {
    updateStatus();
    return;
  }
  refreshScene();
}

void ViewerWindow::cameraChanged(EngineSession& session)
{
  if (&session == session_) applyCamera();
}

void ViewerWindow::viewOptionsChanged(EngineSession& session)
{
  if (&session == session_) refreshScene();
}

}  // namespace mcp
