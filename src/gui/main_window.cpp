#include "main_window.hpp"

#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

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
#include <vtkBillboardTextActor3D.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkFeatureEdges.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkMatrix4x4.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkSmartPointer.h>
#include <vtkSphereSource.h>
#include <vtkTextProperty.h>
#include <vtkTransform.h>
#include <vtkTransformPolyDataFilter.h>
#include <vtkVertexGlyphFilter.h>

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

namespace {

// FragmentMeshの頂点・面（あれば）からvtkPolyDataを構築する。面情報が
// ない入力（点群）は頂点のみのポリデータとしてvtkVertexGlyphFilterで
// 描画可能にする。
vtkSmartPointer<vtkPolyData> buildFragmentPolyData(const FragmentMesh& mesh) {
  auto points = vtkSmartPointer<vtkPoints>::New();
  for (const auto& vertex : mesh.vertices) {
    points->InsertNextPoint(vertex.x, vertex.y, vertex.z);
  }
  auto polyData = vtkSmartPointer<vtkPolyData>::New();
  polyData->SetPoints(points);

  if (!mesh.faces.empty()) {
    auto triangles = vtkSmartPointer<vtkCellArray>::New();
    for (const auto& face : mesh.faces) {
      triangles->InsertNextCell(3);
      triangles->InsertCellPoint(static_cast<vtkIdType>(face.v0));
      triangles->InsertCellPoint(static_cast<vtkIdType>(face.v1));
      triangles->InsertCellPoint(static_cast<vtkIdType>(face.v2));
    }
    polyData->SetPolys(triangles);
    return polyData;
  }

  auto glyphFilter = vtkSmartPointer<vtkVertexGlyphFilter>::New();
  glyphFilter->SetInputData(polyData);
  glyphFilter->Update();
  return glyphFilter->GetOutput();
}

// PropagatedPose（並進＋単位四元数）から同等のvtkMatrix4x4を構築する
// （回転を無視した並進のみの表示は接合結果を誤って表現するため、
// 回転成分も忠実に反映する）。
vtkSmartPointer<vtkMatrix4x4> buildTransformMatrix(const kintsugi::core::PropagatedPose& pose) {
  const double w = pose.rotation.w;
  const double x = pose.rotation.x;
  const double y = pose.rotation.y;
  const double z = pose.rotation.z;
  auto matrix = vtkSmartPointer<vtkMatrix4x4>::New();
  matrix->Identity();
  matrix->SetElement(0, 0, 1.0 - 2.0 * (y * y + z * z));
  matrix->SetElement(0, 1, 2.0 * (x * y - w * z));
  matrix->SetElement(0, 2, 2.0 * (x * z + w * y));
  matrix->SetElement(1, 0, 2.0 * (x * y + w * z));
  matrix->SetElement(1, 1, 1.0 - 2.0 * (x * x + z * z));
  matrix->SetElement(1, 2, 2.0 * (y * z - w * x));
  matrix->SetElement(2, 0, 2.0 * (x * z - w * y));
  matrix->SetElement(2, 1, 2.0 * (y * z + w * x));
  matrix->SetElement(2, 2, 1.0 - 2.0 * (x * x + y * y));
  matrix->SetElement(0, 3, pose.translationMm.x);
  matrix->SetElement(1, 3, pose.translationMm.y);
  matrix->SetElement(2, 3, pose.translationMm.z);
  return matrix;
}

}  // namespace

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
  auto* clearImportButton = new QPushButton(QStringLiteral("インポート済みデータをクリア"), importGroup);
  connect(clearImportButton, &QPushButton::clicked, this, &MainWindow::onClearImportedScans);
  importLayout->addWidget(clearImportButton);
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
  // 新たなインポートは既存のクラスタリング・組み立て結果と整合しなくなるため、
  // 再クラスタリングされるまで古い状態を出力・表示できないよう破棄する。
  resetDerivedClusteringState();
  setStatusMessage(QStringLiteral("インポート完了: 成功 %1 件、失敗 %2 件（合計 %3 件の破片）")
                        .arg(imported)
                        .arg(failed)
                        .arg(importedFragments_.size()));
}

void MainWindow::onClearImportedScans() {
  // インポート済み破片・クラスタリング・組み立て結果を全て破棄し、
  // 新しいスキャンデータを一から取り込めるようにする。
  importedFragments_.clear();
  resetDerivedClusteringState();
  setStatusMessage(QStringLiteral("インポート済みデータをクリアしました。"));
}

void MainWindow::resetDerivedClusteringState() {
  clusteringResult_ = kintsugi::core::ClusteringResult{};
  currentClusterFragments_.clear();
  currentCandidates_.clear();
  orchestrator_.reset();
  dispatcher_.reset();
  clusterCombo_->blockSignals(true);
  clusterCombo_->clear();
  clusterCombo_->blockSignals(false);
  refreshCandidateList();
  refreshViewport();
}

void MainWindow::onRunClustering() {
  if (importedFragments_.empty()) {
    QMessageBox::information(this, QStringLiteral("クラスタリング"),
                              QStringLiteral("先に破片をインポートしてください。"));
    return;
  }
  resetDerivedClusteringState();
  clusteringResult_ = kintsugi::core::clusterFragments(importedFragments_);
  // addItemはcurrentIndexChanged(0)を発火しうるため、全項目投入完了まで
  // シグナルを止め、選択反映（計算コスト有）を一度だけ行う。
  clusterCombo_->blockSignals(true);
  for (std::size_t i = 0; i < clusteringResult_.vesselCandidates.size(); ++i) {
    clusterCombo_->addItem(QStringLiteral("器物候補 %1 (%2片)")
                                .arg(i + 1)
                                .arg(clusteringResult_.vesselCandidates[i].fragments.size()));
  }
  clusterCombo_->blockSignals(false);
  setStatusMessage(QStringLiteral("クラスタリング完了: 器物候補 %1 件、未分類 %2 件")
                        .arg(clusteringResult_.vesselCandidates.size())
                        .arg(clusteringResult_.unclassified.size()));
  if (!clusteringResult_.vesselCandidates.empty()) {
    clusterCombo_->setCurrentIndex(0);
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
  dragFragmentSpin_->setRange(0, fragmentIds.empty() ? 0 : static_cast<int>(fragmentIds.size()) - 1);
  refreshCandidateList();
  refreshViewport();
}

void MainWindow::refreshCandidateList() {
  candidateList_->clear();
  visibleCandidates_.clear();
  if (!orchestrator_) {
    return;
  }
  // 半自動モードの提示契約（信頼度降順・同点は破片ID対昇順）に従い、
  // presentCandidatesが返す整列済みビューをそのまま表示・選択対象とする。
  const auto view = orchestrator_->presentCandidates(currentCandidates_);
  currentCandidates_ = view.candidates;
  const auto& state = orchestrator_->state();
  // 既に採用・却下済みの候補は一覧から除外する（二重採用・undo抜きでの
  // 却下取り消しを防ぐ）。undo/redoで状態が戻れば自動的に一覧へ復帰する。
  auto isDecided = [&state](const JoinCandidate& candidate) {
    for (const auto& join : state.acceptedJoins) {
      if (join.fragmentIdA == candidate.fragmentIdA && join.fragmentIdB == candidate.fragmentIdB) {
        return true;
      }
    }
    for (const auto& rejected : state.rejectedCandidates) {
      if (rejected.fragmentIdA == candidate.fragmentIdA && rejected.fragmentIdB == candidate.fragmentIdB) {
        return true;
      }
    }
    return false;
  };
  for (const JoinCandidate& candidate : currentCandidates_) {
    if (isDecided(candidate)) {
      continue;
    }
    visibleCandidates_.push_back(candidate);
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
  if (row < 0 || static_cast<std::size_t>(row) >= visibleCandidates_.size()) {
    setStatusMessage(QStringLiteral("接合候補を一覧から選択してください。"));
    return;
  }
  const JoinCandidate candidate = visibleCandidates_[static_cast<std::size_t>(row)];
  dispatcher_->acceptCandidate(candidate);
  if (dispatcher_->lastConflictWarning().has_value()) {
    setStatusMessage(QStringLiteral("矛盾のため採用できませんでした（REQ-POTTERY-020）。"));
  } else {
    setStatusMessage(QStringLiteral("接合候補を採用しました。"));
  }
  refreshCandidateList();
  refreshViewport();
}

void MainWindow::onRejectSelectedCandidate() {
  if (!dispatcher_) {
    return;
  }
  const int row = candidateList_->currentRow();
  if (row < 0 || static_cast<std::size_t>(row) >= visibleCandidates_.size()) {
    setStatusMessage(QStringLiteral("接合候補を一覧から選択してください。"));
    return;
  }
  dispatcher_->rejectCandidate(visibleCandidates_[static_cast<std::size_t>(row)]);
  setStatusMessage(QStringLiteral("接合候補を却下しました。"));
  refreshCandidateList();
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
  refreshCandidateList();
  refreshViewport();
}

void MainWindow::onRedo() {
  if (!dispatcher_) {
    return;
  }
  dispatcher_->redo();
  setStatusMessage(QStringLiteral("取り消した操作をやり直しました。"));
  refreshCandidateList();
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
  if (!out.is_open()) {
    QMessageBox::warning(this, QStringLiteral("エクスポート"),
                         QStringLiteral("出力先を開けませんでした: %1").arg(path));
    return;
  }
  out << exported.content;
  out.close();
  if (!out) {
    QMessageBox::warning(this, QStringLiteral("エクスポート"),
                         QStringLiteral("書き込みに失敗しました: %1").arg(path));
    return;
  }
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
      vtkSmartPointer<vtkPolyData> polyData;
      if (fragmentId < currentClusterFragments_.size()) {
        polyData = buildFragmentPolyData(currentClusterFragments_[fragmentId]);
      } else {
        auto sphere = vtkSmartPointer<vtkSphereSource>::New();
        sphere->SetRadius(15.0);
        sphere->Update();
        polyData = sphere->GetOutput();
      }

      auto transform = vtkSmartPointer<vtkTransform>::New();
      if (const auto pose = state.resolvedPose(fragmentId)) {
        transform->SetMatrix(buildTransformMatrix(*pose));
      }
      // 未接合破片は、変換を適用せずスキャン取得時の元の座標のまま表示する。
      // これにより、破片が元の器物内でのおおよその位置関係を保持している
      // スキャンデータ（デモデータ等）では、接合前でも器物全体の形状を
      // 視覚的に把握できる（REQ-POTTERY-014の「組み立て結果を3Dビューア
      // 上に表示」に対する、未接合状態でも意味のある可視化を行うための
      // 実装判断）。
      auto transformFilter = vtkSmartPointer<vtkTransformPolyDataFilter>::New();
      transformFilter->SetTransform(transform);
      transformFilter->SetInputData(polyData);

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
      // メッシュ全体の三角形分割線ではなく、破片の輪郭（他の三角形と
      // 共有されていない境界エッジ = 破片の外周・破損エッジ）のみを
      // 目立つ色で強調表示する。vtkFeatureEdgesは面が1つのセルからしか
      // 参照されていないエッジ（境界エッジ）だけを抽出するため、内部の
      // 三角形分割による格子状の線は表示されず、破片の実際の輪郭形状が
      // 際立つ。点群のみの破片（面情報なし）には境界エッジが存在しない
      // ため、この強調表示は自動的にスキップされる。
      auto featureEdges = vtkSmartPointer<vtkFeatureEdges>::New();
      featureEdges->SetInputConnection(transformFilter->GetOutputPort());
      featureEdges->BoundaryEdgesOn();
      featureEdges->FeatureEdgesOff();
      featureEdges->NonManifoldEdgesOff();
      featureEdges->ManifoldEdgesOff();
      featureEdges->Update();

      auto edgeMapper = vtkSmartPointer<vtkPolyDataMapper>::New();
      edgeMapper->SetInputConnection(featureEdges->GetOutputPort());

      auto edgeActor = vtkSmartPointer<vtkActor>::New();
      edgeActor->SetMapper(edgeMapper);
      edgeActor->GetProperty()->SetColor(1.0, 0.95, 0.1);
      edgeActor->GetProperty()->SetLineWidth(3.0);
      renderer_->AddActor(edgeActor);
      renderer_->AddActor(actor);

      // 破片番号をラベルとして3D空間内に表示する（破片の識別を容易に
      // するため）。vtkBillboardTextActor3Dは常にカメラの方を向くため、
      // 視点を変えても番号が読みやすい。曲面破片ではバウンディングボックス
      // 中心が破片自体の表面よりわずかに内側（裏側）に位置することがあり、
      // その場合ラベルが破片本体に隠れて表示されない問題があった。これを
      // 防ぐため、頂点法線の平均（=破片の外向き方向）に沿ってラベル位置を
      // ごくわずかに表面の外側へオフセットする。オフセット量は破片の
      // バウンディングボックス対角線に比例した小さな値とし、ラベルが
      // 破片から離れて浮いて見えないよう、あくまで表面に貼り付いたように
      // 見える範囲にとどめる。
      transformFilter->Update();
      double bounds[6];
      transformFilter->GetOutput()->GetBounds(bounds);
      const double centerX = (bounds[0] + bounds[1]) / 2.0;
      const double centerY = (bounds[2] + bounds[3]) / 2.0;
      const double centerZ = (bounds[4] + bounds[5]) / 2.0;
      const double diagonal = std::sqrt(
          (bounds[1] - bounds[0]) * (bounds[1] - bounds[0]) +
          (bounds[3] - bounds[2]) * (bounds[3] - bounds[2]) +
          (bounds[5] - bounds[4]) * (bounds[5] - bounds[4]));

      double outwardNormal[3] = {0.0, 0.0, 0.0};
      bool haveNormal = false;
      if (fragmentId < currentClusterFragments_.size()) {
        const auto& mesh = currentClusterFragments_[fragmentId];
        double sumX = 0.0, sumY = 0.0, sumZ = 0.0;
        for (const auto& normal : mesh.normals) {
          sumX += normal.x;
          sumY += normal.y;
          sumZ += normal.z;
        }
        const double length = std::sqrt(sumX * sumX + sumY * sumY + sumZ * sumZ);
        if (length > 1e-6) {
          const double localNormal[3] = {sumX / length, sumY / length, sumZ / length};
          transform->TransformNormal(localNormal, outwardNormal);
          haveNormal = true;
        }
      }
      const double offsetDistance =
          haveNormal ? std::min(std::max(diagonal * 0.06, 1.0), 4.0) : 0.0;
      const double labelX = centerX + outwardNormal[0] * offsetDistance;
      const double labelY = centerY + outwardNormal[1] * offsetDistance;
      const double labelZ = centerZ + outwardNormal[2] * offsetDistance;

      auto label = vtkSmartPointer<vtkBillboardTextActor3D>::New();
      label->SetInput(std::to_string(fragmentId).c_str());
      label->SetPosition(labelX, labelY, labelZ);
      label->GetTextProperty()->SetColor(1.0, 1.0, 1.0);
      label->GetTextProperty()->SetFontSize(18);
      label->GetTextProperty()->SetBold(true);
      label->GetTextProperty()->SetJustificationToCentered();
      label->GetTextProperty()->SetVerticalJustificationToCentered();
      label->PickableOff();
      renderer_->AddActor(label);
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
