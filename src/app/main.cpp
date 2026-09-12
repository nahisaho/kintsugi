// アプリケーションのエントリポイント。DES-POTTERY-009が示す
// 「薄いUIシェル」の一部であり、GuiCommandDispatcher/ViewportCamera等の
// トレース対象コンポーネントを実際のQt/VTK画面へ配線するのみで、独自の
// トレース対象ロジックは持たない（src/gui/main_window.hpp参照）。
#include <QApplication>

#include "../gui/main_window.hpp"

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  kintsugi::gui::MainWindow window;
  window.show();
  return app.exec();
}
