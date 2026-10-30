#include <windows.h>
#include <cstdio>
#include "../model/dvmodel.h"
int wmain(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i) {
        char err[512] = {};
        DvModel* m = dvLoadModel(argv[i], err, sizeof err);
        if (!m) continue;
        auto in = dvModelInfo(m);
        for (uint32_t k = 0; k < in->submeshCount; ++k) {
            auto& s = in->submeshes[k];
            double avg[4] = {};
            if (s.colors) for (uint32_t v = 0; v < s.vertexCount; ++v) for (int c = 0; c < 4; ++c) avg[c] += s.colors[v * 4 + c] / s.vertexCount;
            wprintf(L"%ls #%u base=%.2f,%.2f,%.2f,%.2f vc=%d avg=%.2f,%.2f,%.2f,%.2f tex=%u uv=%d tri=%u\n", argv[i], k, s.baseColor[0], s.baseColor[1], s.baseColor[2], s.baseColor[3],
                    s.colors != nullptr, avg[0], avg[1], avg[2], avg[3], s.textureSize, s.uvs != nullptr, s.indexCount / 3);
        }
        dvFreeModel(m);
    }
}
