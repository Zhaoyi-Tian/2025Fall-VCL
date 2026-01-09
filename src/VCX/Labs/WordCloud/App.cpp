#include "Labs/WordCloud/App.h"

namespace VCX::Labs::labf {
    App::App():
        _ui(
            Labs::Common::UIOptions { }) {
    }

    void App::OnFrame() {
        _ui.Setup(_cases, _caseId);
    }
}
