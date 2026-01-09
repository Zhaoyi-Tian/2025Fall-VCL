#pragma once

#include <vector>

#include "Engine/app.h"
#include "Labs/WordCloud/CaseWC.h"
#include "Labs/Common/UI.h"

namespace VCX::Labs::labf {
    class App : public VCX::Engine::IApp {
    private:
        Common::UI _ui;

        WordCloud   _caseDrawLine;

        std::size_t _caseId = 0;

        std::vector<std::reference_wrapper<Common::ICase>> _cases = {
            _caseDrawLine
        };

    public:
        App();

        void OnFrame() override;
    };
}