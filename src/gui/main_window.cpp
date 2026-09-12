#include "main_window.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_set>

#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QToolTip>
#include <QVBoxLayout>
#include <QWidget>

#include <QVTKOpenGLNativeWidget.h>
#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkFeatureEdges.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkInteractorStyleUser.h>
#include <vtkMatrix4x4.h>
#include <vtkObjectFactory.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkPropPicker.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkSmartPointer.h>
#include <vtkSphereSource.h>
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

// マウスホイールによるズームのみを扱うカスタムインタラクタースタイル。
// vtkInteractorStyleUserを基底とすることで左/中/右ボタンのドラッグに
// よる既定のカメラ回転・パン操作（vtkInteractorStyleTrackballCamera由来）
// は行わず、ホイール操作のみをコールバック経由でViewportCamera（camera_）
// 側のズーム処理に委譲する。これにより、視点操作パネルのボタンと
// マウスホイールの両方が同じcamera_状態を単一の情報源として更新するため、
// どちらの操作を行ってもズームが互いに競合・巻き戻ることがない。
class WheelZoomInteractorStyle : public vtkInteractorStyleUser {
 public:
  static WheelZoomInteractorStyle* New();
  vtkTypeMacro(WheelZoomInteractorStyle, vtkInteractorStyleUser);

  void SetZoomCallback(std::function<void(double)> callback) {
    zoomCallback_ = std::move(callback);
  }

  void OnMouseWheelForward() override {
    if (zoomCallback_) {
      zoomCallback_(kWheelZoomInFactor);
    }
  }

  void OnMouseWheelBackward() override {
    if (zoomCallback_) {
      zoomCallback_(1.0 / kWheelZoomInFactor);
    }
  }

 private:
  static constexpr double kWheelZoomInFactor = 1.1;
  std::function<void(double)> zoomCallback_;
};

vtkStandardNewMacro(WheelZoomInteractorStyle);

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

// 「マッチしない破片」（接合候補が一つも存在しない破片）を並べる画面左の
// スタック配置のパラメータ。カメラの既定姿勢（azimuth=0, elevation=0,
// Z軸を鉛直上向き）では、視点から見て画面左は世界座標のY軸方向のうち
// 焦点から見て+X側にあたるため、+X方向へ壺本体の外側まで離した位置に、
// Z軸方向へ一定間隔で積み上げて配置する。一度に表示するのは
// kStagingVisibleSlots件までとし、それを超える分はビューポート横の
// スクロールバーで縦方向にスクロールして表示範囲を切り替える。
constexpr double kStagingBaseX = 130.0;
constexpr double kStagingSpacingZ = 35.0;
constexpr int kStagingVisibleSlots = 6;
constexpr double kStagingFrameHalfWidthMm = 35.0;
constexpr double kStagingFrameMarginMm = kStagingSpacingZ * 0.5;

// 画面左のスタック表示枠（フレーム）を表す矩形の輪郭線アクターを作る。
// スタック内の何番目からkStagingVisibleSlots件分が現在見えているかを
// 利用者が把握しやすいよう、Y-Z平面（既定視点で画面に正対する面）上に
// 白系の枠線を描く。
vtkSmartPointer<vtkActor> buildStagingFrameActor() {
  // 各破片はz = slot * kStagingSpacingZ (slot = 0..kStagingVisibleSlots-1)に
  // 中心が来るように並ぶため、枠はその範囲を余白付きで包む大きさにする。
  const double zMin = -kStagingSpacingZ * 0.5 - kStagingFrameMarginMm;
  const double zMax = (kStagingVisibleSlots - 1) * kStagingSpacingZ + kStagingSpacingZ * 0.5 +
                       kStagingFrameMarginMm;
  auto points = vtkSmartPointer<vtkPoints>::New();
  points->InsertNextPoint(kStagingBaseX, -kStagingFrameHalfWidthMm, zMin);
  points->InsertNextPoint(kStagingBaseX, kStagingFrameHalfWidthMm, zMin);
  points->InsertNextPoint(kStagingBaseX, kStagingFrameHalfWidthMm, zMax);
  points->InsertNextPoint(kStagingBaseX, -kStagingFrameHalfWidthMm, zMax);

  auto lines = vtkSmartPointer<vtkCellArray>::New();
  lines->InsertNextCell(5);
  lines->InsertCellPoint(0);
  lines->InsertCellPoint(1);
  lines->InsertCellPoint(2);
  lines->InsertCellPoint(3);
  lines->InsertCellPoint(0);

  auto polyData = vtkSmartPointer<vtkPolyData>::New();
  polyData->SetPoints(points);
  polyData->SetLines(lines);

  auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  mapper->SetInputData(polyData);
  auto actor = vtkSmartPointer<vtkActor>::New();
  actor->SetMapper(mapper);
  actor->GetProperty()->SetColor(0.85, 0.9, 0.95);
  actor->GetProperty()->SetLineWidth(2.0);
  return actor;
}

// マウスドラッグ操作中、スクリーン座標(displayX, displayY)を、指定した
// 基準奥行き(referenceDepth、vtkRenderer::WorldToDisplay()のZ成分と同じ
// 正規化デバイス座標系の値)における世界座標へ変換する。ドラッグ開始時に
// ピックした点と同じ奥行きの平面上を、破片がカメラ正面に平行移動する
// ように見えるドラッグ操作を実現するための標準的なVTKの手法。
kintsugi::core::Vec3 displayToWorldAtDepth(vtkRenderer* renderer, double displayX, double displayY,
                                            double referenceDepth) {
  renderer->SetDisplayPoint(displayX, displayY, referenceDepth);
  renderer->DisplayToWorld();
  double world[4];
  renderer->GetWorldPoint(world);
  if (std::abs(world[3]) > 1e-9) {
    return kintsugi::core::Vec3{world[0] / world[3], world[1] / world[3], world[2] / world[3]};
  }
  return kintsugi::core::Vec3{world[0], world[1], world[2]};
}

// 世界座標の1点に対応する、displayToWorldAtDepth()で使う基準奥行き値を求める。
double worldToDisplayDepth(vtkRenderer* renderer, const kintsugi::core::Vec3& worldPoint) {
  renderer->SetWorldPoint(worldPoint.x, worldPoint.y, worldPoint.z, 1.0);
  renderer->WorldToDisplay();
  double display[3];
  renderer->GetDisplayPoint(display);
  return display[2];
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
  viewportWidget_->setMouseTracking(true);
  viewportWidget_->installEventFilter(this);
  auto renderer = vtkSmartPointer<vtkRenderer>::New();
  renderer->SetBackground(0.12, 0.12, 0.15);
  renderWindow->AddRenderer(renderer);
  renderer_ = renderer;
  // マウスドラッグによるVTK既定のカメラ操作（トラックボールスタイルの
  // 回転・パン）は無効化する。これを有効なままにすると、マウス操作で
  // 変更されたカメラ状態がViewportCamera（camera_）側の状態と食い違い、
  // ナビゲーターのボタンを押すたびにrefreshViewport()がcamera_の値で
  // カメラ位置を上書きして変更が元に戻ってしまう問題があった。回転・
  // パンは「視点操作」パネルのボタンのみに一本化することで、camera_を
  // 唯一の状態源にし、この食い違いを防ぐ。一方でマウスホイールによる
  // ズームは、camera_.zoom()を呼び出すコールバックとして残し、
  // 引き続き利用できるようにする（同じcamera_状態を経由するため、
  // ボタン操作と競合しない）。
  if (auto* interactor = viewportWidget_->interactor()) {
    auto style = vtkSmartPointer<WheelZoomInteractorStyle>::New();
    style->SetZoomCallback([this](double factor) { onZoomView(factor); });
    interactor->SetInteractorStyle(style);
  }
  // 「マッチしない破片」の画面左スタック表示を縦方向にスクロールする
  // ためのスクロールバー。ビューポートの左端に隣接させ、視覚的な
  // スタック位置と対応させる。
  auto* viewportContainer = new QWidget(central);
  auto* viewportContainerLayout = new QHBoxLayout(viewportContainer);
  viewportContainerLayout->setContentsMargins(0, 0, 0, 0);
  viewportContainerLayout->setSpacing(2);
  stagingScrollBar_ = new QScrollBar(Qt::Vertical, viewportContainer);
  stagingScrollBar_->setRange(0, 0);
  stagingScrollBar_->setEnabled(false);
  connect(stagingScrollBar_, &QScrollBar::valueChanged, this,
          &MainWindow::onStagingScrollChanged);
  viewportContainerLayout->addWidget(stagingScrollBar_, /*stretch=*/0);
  viewportContainerLayout->addWidget(viewportWidget_, /*stretch=*/1);
  rootLayout->addWidget(viewportContainer, /*stretch=*/3);

  // 右側: 操作パネル。
  auto* panel = new QWidget(central);
  auto* panelLayout = new QVBoxLayout(panel);

  constexpr double kPanStepMm = 20.0;
  constexpr double kZoomInFactor = 1.2;
  constexpr double kZoomOutFactor = 1.0 / kZoomInFactor;
  constexpr double kRotateStepDeg = 15.0;

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

  // 視点操作ナビゲーター（REQ-POTTERY-014の回転・拡大縮小・移動をボタン
  // 操作で行えるようにする）。接合候補パネルの下に配置する。
  auto* navGroup = new QGroupBox(QStringLiteral("視点操作"), panel);
  auto* navLayout = new QVBoxLayout(navGroup);

  auto* zoomRow = new QHBoxLayout();
  auto* zoomInButton = new QPushButton(QStringLiteral("前進"), navGroup);
  connect(zoomInButton, &QPushButton::clicked, this,
          [this]() { onZoomView(kZoomInFactor); });
  zoomRow->addWidget(zoomInButton);
  auto* zoomOutButton = new QPushButton(QStringLiteral("後退"), navGroup);
  connect(zoomOutButton, &QPushButton::clicked, this,
          [this]() { onZoomView(kZoomOutFactor); });
  zoomRow->addWidget(zoomOutButton);
  navLayout->addLayout(zoomRow);

  auto* panRow = new QHBoxLayout();
  auto* panLeftButton = new QPushButton(QStringLiteral("左"), navGroup);
  connect(panLeftButton, &QPushButton::clicked, this,
          [this]() { onPanView(-kPanStepMm, 0.0); });
  panRow->addWidget(panLeftButton);
  auto* panRightButton = new QPushButton(QStringLiteral("右"), navGroup);
  connect(panRightButton, &QPushButton::clicked, this,
          [this]() { onPanView(kPanStepMm, 0.0); });
  panRow->addWidget(panRightButton);
  auto* panUpButton = new QPushButton(QStringLiteral("上"), navGroup);
  connect(panUpButton, &QPushButton::clicked, this,
          [this]() { onPanView(0.0, kPanStepMm); });
  panRow->addWidget(panUpButton);
  auto* panDownButton = new QPushButton(QStringLiteral("下"), navGroup);
  connect(panDownButton, &QPushButton::clicked, this,
          [this]() { onPanView(0.0, -kPanStepMm); });
  panRow->addWidget(panDownButton);
  navLayout->addLayout(panRow);

  auto* rotateRow = new QHBoxLayout();
  auto* rotateLeftButton = new QPushButton(QStringLiteral("左回転"), navGroup);
  connect(rotateLeftButton, &QPushButton::clicked, this,
          [this]() { onRotateView(-kRotateStepDeg, 0.0); });
  rotateRow->addWidget(rotateLeftButton);
  auto* rotateRightButton = new QPushButton(QStringLiteral("右回転"), navGroup);
  connect(rotateRightButton, &QPushButton::clicked, this,
          [this]() { onRotateView(kRotateStepDeg, 0.0); });
  rotateRow->addWidget(rotateRightButton);
  auto* rotateUpButton = new QPushButton(QStringLiteral("上回転"), navGroup);
  connect(rotateUpButton, &QPushButton::clicked, this,
          [this]() { onRotateView(0.0, kRotateStepDeg); });
  rotateRow->addWidget(rotateUpButton);
  auto* rotateDownButton = new QPushButton(QStringLiteral("下回転"), navGroup);
  connect(rotateDownButton, &QPushButton::clicked, this,
          [this]() { onRotateView(0.0, -kRotateStepDeg); });
  rotateRow->addWidget(rotateDownButton);
  navLayout->addLayout(rotateRow);
  panelLayout->addWidget(navGroup);

  // 手動微調整（REQ-POTTERY-008）は、専用パネルではなく3Dビューア上での
  // マウスドラッグ操作（beginFragmentDrag/updateFragmentDrag/endFragmentDrag）
  // で行う。取り消し・やり直しはCtrl+Z/Ctrl+Yのショートカットから行える。
  auto* undoShortcut = new QShortcut(QKeySequence::Undo, this);
  connect(undoShortcut, &QShortcut::activated, this, &MainWindow::onUndo);
  auto* redoShortcut = new QShortcut(QKeySequence::Redo, this);
  connect(redoShortcut, &QShortcut::activated, this, &MainWindow::onRedo);

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

void MainWindow::onRotateView(double deltaAzimuthDeg, double deltaElevationDeg) {
  camera_.rotate(deltaAzimuthDeg, deltaElevationDeg);
  refreshViewport();
}

void MainWindow::onPanView(double dxMm, double dyMm) {
  camera_.pan(dxMm, dyMm);
  refreshViewport();
}

void MainWindow::onZoomView(double factor) {
  camera_.zoom(factor);
  refreshViewport();
}

void MainWindow::onStagingScrollChanged(int value) {
  stagingScrollOffset_ = value;
  refreshViewport();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == viewportWidget_) {
    if (event->type() == QEvent::MouseMove) {
      auto* mouseEvent = static_cast<QMouseEvent*>(event);
      if (draggingFragment_) {
        updateFragmentDrag(mouseEvent->pos());
      } else {
        showFragmentTooltipAt(mouseEvent->pos());
      }
    } else if (event->type() == QEvent::MouseButtonPress) {
      auto* mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseEvent->button() == Qt::LeftButton) {
        beginFragmentDrag(mouseEvent->pos());
      }
    } else if (event->type() == QEvent::MouseButtonRelease) {
      auto* mouseEvent = static_cast<QMouseEvent*>(event);
      if (mouseEvent->button() == Qt::LeftButton && draggingFragment_) {
        endFragmentDrag();
      }
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::showFragmentTooltipAt(const QPoint& widgetPos) {
  // マウス直下の破片を特定し、破片番号をツールチップとして表示する
  // （破片の識別を、ビューア上でのマウスホバーだけで行えるようにする）。
  if (!renderer_ || !viewportWidget_) {
    return;
  }
  // VTKの画面座標は左下原点、Qtのウィジェット座標は左上原点のため、
  // Y座標を反転してから picker に渡す。
  const int vtkY = viewportWidget_->height() - widgetPos.y();
  auto picker = vtkSmartPointer<vtkPropPicker>::New();
  if (picker->PickProp(widgetPos.x(), vtkY, renderer_) == 0) {
    QToolTip::hideText();
    return;
  }
  vtkActor* pickedActor = picker->GetActor();
  const auto it = fragmentActorIds_.find(pickedActor);
  if (it == fragmentActorIds_.end()) {
    QToolTip::hideText();
    return;
  }
  QToolTip::showText(viewportWidget_->mapToGlobal(widgetPos),
                      QStringLiteral("破片 #%1").arg(it->second), viewportWidget_);
}

void MainWindow::beginFragmentDrag(const QPoint& widgetPos) {
  // マウスドラッグによる破片の手動フィット確認（REQ-POTTERY-008）。
  // クリックした破片をピックできた場合のみドラッグを開始する。
  if (!renderer_ || !viewportWidget_ || !dispatcher_) {
    return;
  }
  const int vtkY = viewportWidget_->height() - widgetPos.y();
  auto picker = vtkSmartPointer<vtkPropPicker>::New();
  if (picker->PickProp(widgetPos.x(), vtkY, renderer_) == 0) {
    return;
  }
  vtkActor* pickedActor = picker->GetActor();
  const auto actorIt = fragmentActorIds_.find(pickedActor);
  if (actorIt == fragmentActorIds_.end()) {
    return;
  }
  const std::size_t fragmentId = actorIt->second;
  const auto poseIt = displayedPoses_.find(fragmentId);
  if (poseIt == displayedPoses_.end()) {
    return;
  }
  double pickPos[3];
  picker->GetPickPosition(pickPos);
  const kintsugi::core::Vec3 worldPick{pickPos[0], pickPos[1], pickPos[2]};

  draggingFragment_ = true;
  draggingFragmentId_ = fragmentId;
  draggingRotation_ = poseIt->second.rotation;
  draggingCurrentTranslation_ = poseIt->second.translationMm;
  draggingLastWorldPoint_ = worldPick;
  draggingReferenceDepth_ = worldToDisplayDepth(renderer_, worldPick);
  QToolTip::hideText();
}

void MainWindow::updateFragmentDrag(const QPoint& widgetPos) {
  // ドラッグ開始時にピックした点と同じ奥行きの平面上で破片を追従させる。
  // マウスボタンを離すまではdispatcher_へは反映せず、見た目のみ即時更新
  // する（undoスタックを細かい移動量で埋めないため）。
  if (!renderer_ || !viewportWidget_) {
    return;
  }
  const double vtkY = viewportWidget_->height() - widgetPos.y();
  const kintsugi::core::Vec3 worldPoint =
      displayToWorldAtDepth(renderer_, widgetPos.x(), vtkY, draggingReferenceDepth_);
  draggingCurrentTranslation_.x += worldPoint.x - draggingLastWorldPoint_.x;
  draggingCurrentTranslation_.y += worldPoint.y - draggingLastWorldPoint_.y;
  draggingCurrentTranslation_.z += worldPoint.z - draggingLastWorldPoint_.z;
  draggingLastWorldPoint_ = worldPoint;
  liveDragTranslations_[draggingFragmentId_] = draggingCurrentTranslation_;
  refreshViewport();
}

void MainWindow::endFragmentDrag() {
  // ドラッグ確定：最終位置をdispatcher_->applyManualDrag()で一度だけ反映し、
  // undo/redoの1操作として記録する（REQ-POTTERY-008/REQ-POTTERY-026）。
  const bool wasDragging = draggingFragment_;
  draggingFragment_ = false;
  if (!wasDragging || !dispatcher_) {
    return;
  }
  kintsugi::core::PropagatedPose transform;
  transform.translationMm = draggingCurrentTranslation_;
  transform.rotation = draggingRotation_;
  dispatcher_->applyManualDrag(draggingFragmentId_, transform);
  liveDragTranslations_.erase(draggingFragmentId_);
  setStatusMessage(
      QStringLiteral("マウスドラッグで破片 #%1 を移動しました。").arg(draggingFragmentId_));
  refreshViewport();
}

void MainWindow::refreshViewport() {
  if (!renderer_) {
    return;
  }
  renderer_->RemoveAllViewProps();
  fragmentActorIds_.clear();
  displayedPoses_.clear();
  if (orchestrator_) {
    const auto& state = orchestrator_->state();

    // 接合候補計算（currentCandidates_）で一件も候補が得られなかった破片は
    // 「マッチしない破片」（壺本体とは別由来と想定）とみなし、画面左に
    // 一列にまとめて表示する。接合候補が1件以上ある破片は、未接合でも
    // 従来通りスキャン取得時の元の座標のまま表示し、器物全体の形状を
    // 視覚的に把握できるようにする。
    std::unordered_set<std::size_t> fragmentsWithCandidate;
    for (const auto& candidate : currentCandidates_) {
      fragmentsWithCandidate.insert(candidate.fragmentIdA);
      fragmentsWithCandidate.insert(candidate.fragmentIdB);
    }

    // マッチしない破片のうち、まだ手動で動かされていない（resolvedPoseを
    // 持たない）ものだけがスタック表示の対象になる。件数が
    // kStagingVisibleSlotsを超える場合に備え、先に対象一覧を確定して
    // スクロールバーの可動範囲を設定する。
    std::vector<std::size_t> stagingFragmentIds;
    for (std::size_t fragmentId : state.fragmentIds) {
      if (!state.resolvedPose(fragmentId) && fragmentsWithCandidate.count(fragmentId) == 0) {
        stagingFragmentIds.push_back(fragmentId);
      }
    }
    const int maxStagingOffset =
        std::max(0, static_cast<int>(stagingFragmentIds.size()) - kStagingVisibleSlots);
    stagingScrollOffset_ = std::clamp(stagingScrollOffset_, 0, maxStagingOffset);
    if (stagingScrollBar_) {
      const QSignalBlocker blocker(stagingScrollBar_);
      stagingScrollBar_->setEnabled(maxStagingOffset > 0);
      stagingScrollBar_->setRange(0, maxStagingOffset);
      stagingScrollBar_->setPageStep(std::max(1, kStagingVisibleSlots));
      stagingScrollBar_->setValue(stagingScrollOffset_);
    }
    std::unordered_map<std::size_t, int> stagingVisibleSlot;
    for (std::size_t i = 0; i < stagingFragmentIds.size(); ++i) {
      const int visibleSlot = static_cast<int>(i) - stagingScrollOffset_;
      if (visibleSlot >= 0 && visibleSlot < kStagingVisibleSlots) {
        stagingVisibleSlot[stagingFragmentIds[i]] = visibleSlot;
      }
    }
    if (!stagingFragmentIds.empty()) {
      renderer_->AddActor(buildStagingFrameActor());
    }

    for (std::size_t fragmentId : state.fragmentIds) {
      const bool isNonMatching = fragmentsWithCandidate.count(fragmentId) == 0;
      const auto stagingSlotIt = stagingVisibleSlot.find(fragmentId);
      if (isNonMatching && !state.resolvedPose(fragmentId) &&
          stagingSlotIt == stagingVisibleSlot.end()) {
        // スクロール範囲外のマッチしない破片は、スクロールバーで表示
        // 範囲に入れるまで描画しない。
        continue;
      }
      vtkSmartPointer<vtkPolyData> polyData;
      if (fragmentId < currentClusterFragments_.size()) {
        polyData = buildFragmentPolyData(currentClusterFragments_[fragmentId]);
      } else {
        auto sphere = vtkSmartPointer<vtkSphereSource>::New();
        sphere->SetRadius(15.0);
        sphere->Update();
        polyData = sphere->GetOutput();
      }

      kintsugi::core::PropagatedPose displayPose;
      const auto resolvedPose = state.resolvedPose(fragmentId);
      if (resolvedPose) {
        // 採用済み接合、またはマウスドラッグ等による手動姿勢設定済み
        // （元がマッチしない破片としてスタック配置されていた場合を含む）。
        displayPose = *resolvedPose;
      } else if (isNonMatching) {
        // マッチしない破片：画面左のスタック枠内の現在の表示スロットに
        // 配置する。
        displayPose.translationMm = Vec3{
            kStagingBaseX, 0.0, static_cast<double>(stagingSlotIt->second) * kStagingSpacingZ};
      }
      // resolvedPoseもなく、接合候補が存在する未接合破片は、変換を適用
      // せずスキャン取得時の元の座標のまま表示する（displayPoseは恒等姿勢
      // のまま）。
      const auto liveIt = liveDragTranslations_.find(fragmentId);
      if (liveIt != liveDragTranslations_.end()) {
        // ドラッグ中の破片は、確定前の見た目としてライブの並進値で上書き
        // する（回転は元のまま）。
        displayPose.translationMm = liveIt->second;
      }
      displayedPoses_[fragmentId] = displayPose;

      auto transform = vtkSmartPointer<vtkTransform>::New();
      transform->SetMatrix(buildTransformMatrix(displayPose));
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
      } else if (isNonMatching) {
        // マッチしない破片であることが一目で分かるよう、接合済み（緑）・
        // 未接合だが候補あり（オレンジ）とは明確に異なる目立つ紫色にする
        // （従来のグレーは背景・他破片と紛れやすかったため変更）。
        actor->GetProperty()->SetColor(0.75, 0.2, 0.85);
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
      // マウスホバー時のツールチップ表示、およびマウスドラッグ開始時の
      // 破片特定のため、実体アクターと破片IDの対応を記録する（境界エッジ
      // 用アクターは対象外）。破片番号は常時表面に表示するのではなく、
      // ホバー時のツールチップのみで示す。
      fragmentActorIds_[actor.Get()] = fragmentId;
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
