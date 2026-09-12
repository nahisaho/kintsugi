#pragma once

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include <QMainWindow>
#include <QPoint>

#include "../core/assembly_orchestrator.hpp"
#include "../core/fragment_clustering.hpp"
#include "../core/fragment_matching.hpp"
#include "../core/fragment_mesh.hpp"
#include "gui_command_dispatcher.hpp"
#include "viewport_camera.hpp"

class QComboBox;
class QListWidget;
class QLabel;
class QVTKOpenGLNativeWidget;
class vtkActor;
class vtkRenderer;

namespace kintsugi::gui {

// 本クラスおよびmain.cppは、DES-POTTERY-009が「本コンポーネントの外側」と
// 位置づける薄いQt/VTK UIシェルであり、GuiCommandDispatcher/ViewportCameraの
// ような独立した設計コンポーネント（@id/@design付きのトレース対象）ではない。
// 既存のヘッドレス（GUI非依存）コンポーネントを実際の画面へ配線するだけの
// 適応であり、REQ-POTTERY-017の「Windows実機での起動・操作確認」自体の
// 自動テスト証跡は本開発環境（Linux, ディスプレイなし実行を含む）では
// 作成できないため、Issue #2で引き続き追跡する。本シェル自体はLinux上の
// 実ディスプレイ環境で起動・操作できることを手動で確認済み。
class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

 private slots:
  void onImportScans();
  void onClearImportedScans();
  void onRunClustering();
  void onClusterSelectionChanged(int index);
  void onAcceptSelectedCandidate();
  void onRejectSelectedCandidate();
  void onUndo();
  void onRedo();
  void onExportMesh();
  void onExportMatchedFragmentIds();
  void onRotateView(double deltaAzimuthDeg, double deltaElevationDeg);
  void onPanView(double dxMm, double dyMm);
  void onZoomView(double factor);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  void buildUi();
  void refreshCandidateList();
  void refreshViewport();
  void setStatusMessage(const QString& message);
  void resetDerivedClusteringState();
  void showFragmentTooltipAt(const QPoint& widgetPos);
  void beginFragmentDrag(const QPoint& widgetPos);
  void updateFragmentDrag(const QPoint& widgetPos);
  void endFragmentDrag();

  std::vector<kintsugi::core::FragmentMesh> importedFragments_;
  kintsugi::core::ClusteringResult clusteringResult_;

  std::vector<kintsugi::core::FragmentMesh> currentClusterFragments_;
  std::vector<kintsugi::core::JoinCandidate> currentCandidates_;
  // refreshCandidateList()が一覧に表示した（未決定の）候補のみのビュー。
  // candidateList_の行番号はこちらに対応する（currentCandidates_全体には
  // 対応しない。決定済み候補は非表示のため）。
  std::vector<kintsugi::core::JoinCandidate> visibleCandidates_;
  std::unique_ptr<kintsugi::core::AssemblyOrchestrator> orchestrator_;
  std::unique_ptr<GuiCommandDispatcher> dispatcher_;
  // 既定距離500mmでは、壺本体（半径50mm程度）に加えて画面左のマッチしない
  // 破片スタック（kStagingBaseX付近）まで含めた範囲がビューポートの
  // アスペクト比によっては画角に収まりきらないことがあったため、
  // 既定距離を広めに取り、双方が最初から視野に入るようにする。
  ViewportCamera camera_{Point3D{0.0, 0.0, 0.0}, 650.0};

  QComboBox* clusterCombo_ = nullptr;
  QListWidget* candidateList_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QVTKOpenGLNativeWidget* viewportWidget_ = nullptr;
  vtkRenderer* renderer_ = nullptr;
  // マウスホバーによる破片番号表示（ツールチップ）用に、直近の
  // refreshViewport()で描画した各破片の本体アクターと破片IDの対応を
  // 保持する。ポインタは同じ関数内で再構築されるためrefreshViewport()の
  // 呼び出しごとにクリアする。
  std::unordered_map<vtkActor*, std::size_t> fragmentActorIds_;
  // 直近のrefreshViewport()で各破片に実際に適用した姿勢（接合候補ゼロの
  // 「マッチしない破片」の画面左スタックへの配置、または未接合破片の元の
  // スキャン座標を含む）。マウスドラッグ開始時に、ドラッグ対象破片の現在の
  // 表示姿勢を基準として使うために保持する。
  std::unordered_map<std::size_t, kintsugi::core::PropagatedPose> displayedPoses_;
  // 3Dビューア上でのマウスドラッグによる破片の手動移動
  // （REQ-POTTERY-008の操作手段追加）用の状態。ドラッグ中は見た目のみを
  // liveDragTranslations_で即時反映し、マウスボタンを離した時点で一度だけ
  // dispatcher_->applyManualDrag()を呼んでundo/redoの1操作として確定する
  // （マウス移動のたびに確定させるとundoスタックが大量の細かい操作で
  // 埋まってしまうため）。
  bool draggingFragment_ = false;
  std::size_t draggingFragmentId_ = 0;
  kintsugi::core::Quaternion draggingRotation_;
  kintsugi::core::Vec3 draggingCurrentTranslation_;
  kintsugi::core::Vec3 draggingLastWorldPoint_;
  double draggingReferenceDepth_ = 0.0;
  std::unordered_map<std::size_t, kintsugi::core::Vec3> liveDragTranslations_;
};

}  // namespace kintsugi::gui
