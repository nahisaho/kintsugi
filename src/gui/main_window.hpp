#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <QMainWindow>

#include "../core/assembly_orchestrator.hpp"
#include "../core/fragment_clustering.hpp"
#include "../core/fragment_matching.hpp"
#include "../core/fragment_mesh.hpp"
#include "gui_command_dispatcher.hpp"
#include "viewport_camera.hpp"

class QComboBox;
class QListWidget;
class QDoubleSpinBox;
class QSpinBox;
class QLabel;
class QVTKOpenGLNativeWidget;
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
  void onApplyManualDrag();
  void onUndo();
  void onRedo();
  void onExportMesh();

 private:
  void buildUi();
  void refreshCandidateList();
  void refreshViewport();
  void setStatusMessage(const QString& message);
  void resetDerivedClusteringState();

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
  ViewportCamera camera_;

  QComboBox* clusterCombo_ = nullptr;
  QListWidget* candidateList_ = nullptr;
  QSpinBox* dragFragmentSpin_ = nullptr;
  QDoubleSpinBox* dragDxSpin_ = nullptr;
  QDoubleSpinBox* dragDySpin_ = nullptr;
  QDoubleSpinBox* dragDzSpin_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QVTKOpenGLNativeWidget* viewportWidget_ = nullptr;
  vtkRenderer* renderer_ = nullptr;
};

}  // namespace kintsugi::gui
