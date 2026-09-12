#include "main_window.hpp"

#include <cmath>
#include <fstream>
#include <sstream>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkSmartPointer.h>
#include <vtkSphereSource.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>

#include "../core/mesh_export.hpp"
#include "../core/scan_importer.hpp"

namespace kintsugi::gui {

using kintsugi::core::AssemblyOrchestrator;
using kintsugi::core::ClusteringResult;
using kintsugi::core::FragmentMesh;
using kintsugi::core::JoinCandidate;
using kintsugi::core::MeshExportFormat;
using kintsugi::core::PropagatedPose;
using kintsugi::core::Vec3;

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  buildUi();
  setWindowTitle(QStringLiteral("Kintsugi - 土器破片接合シミュレーション"));
  resize(1024, 768);
  setStatusMessage(QStringLiteral("スキャンデータをインポートしてください。"));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildUi() {
  auto* central = new QWidget(this);
  auto* rootLayout = new QHBoxLayout(central);

  // 左側: 3Dビューポート（VTK, ADR-0002）。
  auto renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();
  viewportWidget_ = new QVTKOpenGLNativeWidget(central);
  viewportWidget_->setRenderWindow(renderWindow);
  auto renderer = vtkSmartPointer<vtkRenderer>::New();
  renderer->SetBackground(0.12, 0.12, 0.15);
  renderWindow->AddRenderer(renderer);
  renderer_ = renderer;
  rootLayout->addWidget(viewportWidget_, /*stretch=*/3);

  // 右側: 操作パネル。
  auto* panel = new QWidget(central);
  auto* panelLayout = new QVBoxLayout(panel);

  auto* importGroup = new QGroupBox(QStringLiteral("1. インポート・クラスタリング"), panel);
  auto* importLayout = new QVBoxLayout(importGroup);
  auto* importButton = new QPushButton(QStringLiteral("スキャンをインポート..."), importGroup);
  connect(importButton, &QPushButton::clicked, this, &MainWindow::onImportScans);
  importLayout->addWidget(importButton);
  auto* clusterButton = new QPushButton(QStringLiteral("クラスタリング実行"), importGroup);
  connect(clusterButton, &QPushButton::clicked, this, &MainWindow::onRunClustering);
  importLayout->addWidget(clusterButton);
  clusterCombo_ = new QComboBox(importGroup);
  connect(clusterCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &MainWindow::onClusterSelectionChanged);
  importLayout->addWidget(clusterCombo_);
  panelLayout->addWidget(importGroup);

  auto* candidateGroup = new QGroupBox(QStringLiteral("2. 接合候補"), panel);
  auto* candidateLayout = new QVBoxLayout(candidateGroup);
  candidateList_ = new QListWidget(candidateGroup);
  candidateLayout->addWidget(candidateList_);
  auto* candidateButtons = new QHBoxLayout();
  auto* acceptButton = new QPushButton(QStringLiteral("採用"), candidateGroup);
  connect(acceptButton, &QPushButton::clicked, this, &MainWindow::onAcceptSelectedCandidate);
  candidateButtons->addWidget(acceptButton);
  auto* rejectButton = new QPushButton(QStringLiteral("却下"), candidateGroup);
  connect(rejectButton, &QPushButton::clicked, this, &MainWindow::onRejectSelectedCandidate);
  candidateButtons->addWidget(rejectButton);
  candidateLayout->addLayout(candidateButtons);
  panelLayout->addWidget(candidateGroup);

  auto* manualGroup = new QGroupBox(QStringLiteral("3. 手動微調整（REQ-POTTERY-008）"), panel);
  auto* manualLayout = new QVBoxLayout(manualGroup);
  auto* fragmentRow = new QHBoxLayout();
  fragmentRow->addWidget(new QLabel(QStringLiteral("破片ID:"), manualGroup));
  dragFragmentSpin_ = new QSpinBox(manualGroup);
  dragFragmentSpin_->setRange(0, 100000);
  fragmentRow->addWidget(dragFragmentSpin_);
  manualLayout->addLayout(fragmentRow);
  auto* dxyzRow = new QHBoxLayout();
  dragDxSpin_ = new QDoubleSpinBox(manualGroup);
  dragDxSpin_->setRange(-100000.0, 100000.0);
  dragDxSpin_->setPrefix(QStringLiteral("dx="));
  dragDySpin_ = new QDoubleSpinBox(manualGroup);
  dragDySpin_->setRange(-100000.0, 100000.0);
  dragDySpin_->setPrefix(QStringLiteral("dy="));
  dragDzSpin_ = new QDoubleSpinBox(manualGroup);
  dragDzSpin_->setRange(-100000.0, 100000.0);
  dragDzSpin_->setPrefix(QStringLiteral("dz="));
  dxyzRow->addWidget(dragDxSpin_);
  dxyzRow->addWidget(dragDySpin_);
  dxyzRow->addWidget(dragDzSpin_);
  manualLayout->addLayout(dxyzRow);
  auto* applyDragButton = new QPushButton(QStringLiteral("手動移動を適用"), manualGroup);
  connect(applyDragButton, &QPushButton::clicked, this, &MainWindow::onApplyManualDrag);
  manualLayout->addWidget(applyDragButton);
  auto* undoRedoRow = new QHBoxLayout();
  auto* undoButton = new QPushButton(QStringLiteral("元に戻す(undo)"), manualGroup);
  connect(undoButton, &QPushButton::clicked, this, &MainWindow::onUndo);
  undoRedoRow->addWidget(undoButton);
  auto* redoButton = new QPushButton(QStringLiteral("やり直す(redo)"), manualGroup);
  connect(redoButton, &QPushButton::clicked, this, &MainWindow::onRedo);
  undoRedoRow->addWidget(redoButton);
  manualLayout->addLayout(undoRedoRow);
  panelLayout->addWidget(manualGroup);

  auto* exportGroup = new QGroupBox(QStringLiteral("4. 出力"), panel);
  auto* exportLayout = new QVBoxLayout(exportGroup);
  auto* exportButton = new QPushButton(QStringLiteral("統合メッシュをエクスポート..."), exportGroup);
  connect(exportButton, &QPushButton::clicked, this, &MainWindow::onExportMesh);
  exportLayout->addWidget(exportButton);
  panelLayout->addWidget(exportGroup);

  statusLabel_ = new QLabel(panel);
  statusLabel_->setWordWrap(true);
  panelLayout->addWidget(statusLabel_);
  panelLayout->addStretch(1);

  rootLayout->addWidget(panel, /*stretch=*/2);
  setCentralWidget(central);
}

void MainWindow::setStatusMessage(const QString& message) {
  if (statusLabel_) {
    statusLabel_->setText(message);
  }
}

void MainWindow::onImportScans() {
  const QStringList paths = QFileDialog::getOpenFileNames(
      this, QStringLiteral("破片スキャンデータを選択"), QString(),
      QStringLiteral("スキャンデータ (*.pcd *.stl *.obj);;すべてのファイル (*)"));
  if (paths.isEmpty()) {
    return;
  }
  int imported = 0;
  int failed = 0;
  for (const QString& path : paths) {
    auto result = kintsugi::core::importFragment(path.toStdString());
    if (auto* mesh = std::get_if<FragmentMesh>(&result)) {
      importedFragments_.push_back(*mesh);
      ++imported;
    } else {
      ++failed;
    }
  }
  setStatusMessage(QStringLiteral("インポート完了: 成功 %1 件、失敗 %2 件（合計 %3 件の破片）")
                        .arg(imported)
                        .arg(failed)
                        .arg(importedFragments_.size()));
}

void MainWindow::onRunClustering() {
  if (importedFragments_.empty()) {
    QMessageBox::information(this, QStringLiteral("クラスタリング"),
                              QStringLiteral("先に破片をインポートしてください。"));
    return;
  }
  clusteringResult_ = kintsugi::core::clusterFragments(importedFragments_);
  clusterCombo_->clear();
  for (std::size_t i = 0; i < clusteringResult_.vesselCandidates.size(); ++i) {
    clusterCombo_->addItem(QStringLiteral("器物候補 %1 (%2片)")
                                .arg(i + 1)
                                .arg(clusteringResult_.vesselCandidates[i].fragments.size()));
  }
  setStatusMessage(QStringLiteral("クラスタリング完了: 器物候補 %1 件、未分類 %2 件")
                        .arg(clusteringResult_.vesselCandidates.size())
                        .arg(clusteringResult_.unclassified.size()));
  if (!clusteringResult_.vesselCandidates.empty()) {
    onClusterSelectionChanged(0);
  }
}

void MainWindow::onClusterSelectionChanged(int index) {
  if (index < 0 || static_cast<std::size_t>(index) >= clusteringResult_.vesselCandidates.size()) {
    return;
  }
  currentClusterFragments_ = clusteringResult_.vesselCandidates[static_cast<std::size_t>(index)].fragments;
  currentCandidates_ = kintsugi::core::computeJoinCandidates(currentClusterFragments_);
  std::vector<std::size_t> fragmentIds(currentClusterFragments_.size());
  for (std::size_t i = 0; i < fragmentIds.size(); ++i) {
    fragmentIds[i] = i;
  }
  orchestrator_ = std::make_unique<AssemblyOrchestrator>(fragmentIds);
  dispatcher_ = std::make_unique<GuiCommandDispatcher>(*orchestrator_);
  refreshCandidateList();
  refreshViewport();
}

void MainWindow::refreshCandidateList() {
  candidateList_->clear();
  for (const JoinCandidate& candidate : currentCandidates_) {
    candidateList_->addItem(QStringLiteral("破片%1 - 破片%2 (信頼度 %3)")
                                 .arg(candidate.fragmentIdA)
                                 .arg(candidate.fragmentIdB)
                                 .arg(candidate.confidenceScore, 0, 'f', 1));
  }
}

void MainWindow::onAcceptSelectedCandidate() {
  if (!dispatcher_) {
    return;
  }
  const int row = candidateList_->currentRow();
  if (row < 0 || static_cast<std::size_t>(row) >= currentCandidates_.size()) {
    setStatusMessage(QStringLiteral("接合候補を一覧から選択してください。"));
    return;
  }
  dispatcher_->acceptCandidate(currentCandidates_[static_cast<std::size_t>(row)]);
  if (dispatcher_->lastConflictWarning().has_value()) {
    setStatusMessage(QStringLiteral("矛盾のため採用できませんでした（REQ-POTTERY-020）。"));
  } else {
    setStatusMessage(QStringLiteral("接合候補を採用しました。"));
  }
  refreshViewport();
}

void MainWindow::onRejectSelectedCandidate() {
  if (!dispatcher_) {
    return;
  }
  const int row = candidateList_->currentRow();
  if (row < 0 || static_cast<std::size_t>(row) >= currentCandidates_.size()) {
    setStatusMessage(QStringLiteral("接合候補を一覧から選択してください。"));
    return;
  }
  dispatcher_->rejectCandidate(currentCandidates_[static_cast<std::size_t>(row)]);
  setStatusMessage(QStringLiteral("接合候補を却下しました。"));
  refreshViewport();
}

void MainWindow::onApplyManualDrag() {
  if (!dispatcher_) {
    setStatusMessage(QStringLiteral("先にクラスタリングを実行してください。"));
    return;
  }
  PropagatedPose transform;
  transform.translationMm =
      Vec3{dragDxSpin_->value(), dragDySpin_->value(), dragDzSpin_->value()};
  dispatcher_->applyManualDrag(static_cast<std::size_t>(dragFragmentSpin_->value()), transform);
  setStatusMessage(QStringLiteral("手動微調整を適用しました。"));
  refreshViewport();
}

void MainWindow::onUndo() {
  if (!dispatcher_) {
    return;
  }
  dispatcher_->undo();
  setStatusMessage(QStringLiteral("直前の操作を取り消しました。"));
  refreshViewport();
}

void MainWindow::onRedo() {
  if (!dispatcher_) {
    return;
  }
  dispatcher_->redo();
  setStatusMessage(QStringLiteral("取り消した操作をやり直しました。"));
  refreshViewport();
}

void MainWindow::onExportMesh() {
  if (!orchestrator_) {
    QMessageBox::information(this, QStringLiteral("エクスポート"),
                              QStringLiteral("先にクラスタリングを実行してください。"));
    return;
  }
  const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("統合メッシュを出力"), QString(),
      QStringLiteral("STLファイル (*.stl);;OBJファイル (*.obj)"));
  if (path.isEmpty()) {
    return;
  }
  const MeshExportFormat format = path.endsWith(QStringLiteral(".obj"), Qt::CaseInsensitive)
                                       ? MeshExportFormat::Obj
                                       : MeshExportFormat::Stl;
  const auto exported =
      kintsugi::core::exportIntegratedMesh(orchestrator_->state(), currentClusterFragments_, format);
  std::ofstream out(path.toStdString(), std::ios::binary);
  out << exported.content;
  out.close();
  setStatusMessage(QStringLiteral("エクスポートしました: %1").arg(path));
}

void MainWindow::refreshViewport() {
  if (!renderer_) {
    return;
  }
  renderer_->RemoveAllViewProps();
  if (orchestrator_) {
    const auto& state = orchestrator_->state();
    for (std::size_t fragmentId : state.fragmentIds) {
      auto sphere = vtkSmartPointer<vtkSphereSource>::New();
      sphere->SetRadius(15.0);
      sphere->SetThetaResolution(16);
      sphere->SetPhiResolution(16);

      auto transform = vtkSmartPointer<vtkTransform>::New();
      if (const auto pose = state.resolvedPose(fragmentId)) {
        transform->Translate(pose->translationMm.x, pose->translationMm.y, pose->translationMm.z);
      } else {
        // 未接合破片は原点付近に並べて表示する（可視化上の適応）。
        transform->Translate(static_cast<double>(fragmentId) * 40.0, 0.0, 0.0);
      }
      auto transformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
      transformFilter->SetTransform(transform);
      transformFilter->SetInputConnection(sphere->GetOutputPort());

      auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
      mapper->SetInputConnection(transformFilter->GetOutputPort());

      auto actor = vtkSmartPointer<vtkActor>::New();
      actor->SetMapper(mapper);
      const bool joined = state.hasAcceptedJoin(fragmentId);
      if (joined) {
        actor->GetProperty()->SetColor(0.2, 0.7, 0.3);
      } else {
        actor->GetProperty()->SetColor(0.7, 0.4, 0.2);
      }
      renderer_->AddActor(actor);
    }
  }

  vtkCamera* vtkCam = renderer_->GetActiveCamera();
  const auto eye = camera_.eyePosition();
  const auto focal = camera_.focalPoint();
  vtkCam->SetPosition(eye.x, eye.y, eye.z);
  vtkCam->SetFocalPoint(focal.x, focal.y, focal.z);
  vtkCam->SetViewUp(0.0, 0.0, 1.0);
  renderer_->ResetCameraClippingRange();
  if (viewportWidget_ && viewportWidget_->renderWindow()) {
    viewportWidget_->renderWindow()->Render();
  }
}

}  // namespace kintsugi::gui
