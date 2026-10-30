// DeepImage Viewer — Direct3D 11 model viewport
// Copyright (c) DMIND <dmind114@gmail.com>. MIT License.
#pragma once
#include "common.h"
#include "../model/dvmodel.h"

namespace dv {

struct V3 { float x = 0, y = 0, z = 0; };

struct ModelStats {
    uint32_t meshes = 0, materials = 0;
    uint64_t vertices = 0, triangles = 0;
    float extent[3]{};
    int units = 0;
};

class ModelView {
public:
    ~ModelView();
    bool setModel(const DvModelInfo* info);   // uploads geometry (UI thread)
    void clear();
    bool hasModel() const { return !parts_.empty(); }

    // rendering
    bool render(UINT w, UINT h);              // renders into offscreen texture if dirty; true if a new frame was produced
    ID2D1Bitmap1* bitmap() const { return bmp_.Get(); }
    bool animating() const;                   // camera still moving / auto-rotate
    void invalidate() { dirty_ = true; }
    ComPtr<IWICBitmap> screenshot(UINT w, UINT h);

    // camera control
    void orbit(float dx, float dy);
    void pan(float dx, float dy, float viewH);
    void zoom(float wheelDelta);
    void view(const char* name, bool animate = true);
    void viewDir(V3 dir, bool animate = true);
    void viewModelAxis(int axis, bool negative);

    // display flags
    bool wire = false, edges = false, clay = false, grid = true, bbox = false, autoRotate = false;
    void setEdges(bool on);
    void toggleUp();
    int upAxis() const { return up_; }
    void setTheme(bool dark, D2D1_COLOR_F accent) { dark_ = dark; accent_ = accent; dirty_ = true; buildGrid(); buildBox(); }

    ModelStats stats;
    // gizmo: screen-space directions (x right, y down) and depth for world X/Y/Z in model convention
    void axisDirs(float out[3][3]) const;

private:
    struct Part {
        ComPtr<ID3D11Buffer> vb, ib;
        ComPtr<ID3D11ShaderResourceView> tex;
        UINT indexCount = 0;
        float color[4];
        bool points = false;
        bool vertexColors = false;
    };
    bool initPipeline();
    void ensureTargets(UINT w, UINT h);
    void draw(ID3D11RenderTargetView* rtv, ID3D11DepthStencilView* dsv, UINT w, UINT h, UINT samples);
    void buildGrid();
    void buildBox();
    void buildEdges();
    void computeFrame();
    void updateCamera(float dt);
    void matrices(UINT w, UINT h, float* viewProj, V3& eye) const;

    std::vector<Part> parts_;
    // CPU copies for edges (positions in model space)
    std::vector<std::vector<float>> cpuPos_;
    std::vector<std::vector<uint32_t>> cpuIdx_;
    std::vector<bool> cpuPoints_;

    ComPtr<ID3D11VertexShader> vsMesh_, vsLine_;
    ComPtr<ID3D11PixelShader> psMesh_, psLine_, psShadow_;
    ComPtr<ID3D11InputLayout> ilMesh_, ilLine_;
    ComPtr<ID3D11Buffer> cb_;
    ComPtr<ID3D11RasterizerState> rsSolid_, rsWire_, rsLine_;
    ComPtr<ID3D11BlendState> bsAlpha_, bsOpaque_;
    ComPtr<ID3D11DepthStencilState> dsOn_, dsNoWrite_;
    ComPtr<ID3D11SamplerState> sampler_;
    ComPtr<ID3D11Buffer> gridVb_, boxVb_, edgeVb_, shadowVb_;
    UINT gridCount_ = 0, boxCount_ = 0, edgeCount_ = 0;

    ComPtr<ID3D11Texture2D> msColor_, msDepth_, resolved_;
    ComPtr<ID3D11RenderTargetView> msRtv_;
    ComPtr<ID3D11DepthStencilView> msDsv_;
    ComPtr<ID2D1Bitmap1> bmp_;
    UINT tw_ = 0, th_ = 0, samples_ = 1;
    bool ready_ = false, dirty_ = true;

    // model frame
    int up_ = 1;               // 1 = Y up, 2 = Z up
    float bmin_[3]{}, bmax_[3]{};
    float model_[16]{};        // model -> world
    V3 center_;
    float radius_ = 1;

    // camera (current / goal)
    float yaw_ = 0.8f, pitch_ = 0.5f, dist_ = 3, gyaw_ = 0.8f, gpitch_ = 0.5f, gdist_ = 3;
    V3 target_, gtarget_;
    DWORD lastTick_ = 0;
    bool dark_ = true;
    D2D1_COLOR_F accent_{};
};

} // namespace dv
