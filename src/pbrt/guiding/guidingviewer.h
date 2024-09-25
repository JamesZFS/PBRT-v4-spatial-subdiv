//
// Created by fengshi on 9/11/24.
//

#ifndef GUIDINGVIEWER_H
#define GUIDINGVIEWER_H

#include <pbrt/pbrt.h>
#include <pbrt/scene.h>
#include <pbrt/cameras.h>

namespace openpgl {
namespace cpp {
    struct Field;
}
}

namespace pbrt {

void RenderGuidingViewer(BasicScene &scene);

class GuidingViewerGUI {
public:
    GuidingViewerGUI(
        Camera camera, Primitive scene, openpgl::cpp::Field* field, int spp,
        const std::function<void(int waveStart)> &renderWave,
        const std::function<void(int waveEnd)> &postprocessWave,
        const std::function<void(int waveEnd)> &saveImage);
    ~GuidingViewerGUI();
    void Launch();  // The GUI runs in the main thread, while the rendering runs in a separate thread.
    int RenderedWaves() const { return waveStart; }
    void ClearFilm();

    enum RendererState {
        Initial = 0,
        Rendering,
        WaveEnd,
        Completed
    };

    enum GUICommand {
        // Control buttons begin
        AutoPlay = 0,
        Pause,
        Forward,
        Save,
        Restart,
        // Control buttons end
        Terminate,
        None,
    };

    enum SelectedChannel {
        Channel_Radiance = 0,
        Channel_CacheID,
        Channel_Fluence,
        Channel_CE,
        Channel_Count,
    };

    enum CMaps {
        CMap_Cividis = 0,
        CMap_Inferno,
        CMap_Magma,
        CMap_Plasma,
        CMap_Viridis,
        CMap_Count,
    };

private:
    void RenderThread();
    void UpdateCPUFramebufferFromFilm();
    void UpdateGPUFramebufferFromCPU();
    void Tab();
    void Canvas();
    void Inspector();
    void StatusBar();
    void CacheCurves();
    void UpdateRayCastingResult();
    void ResetCECurves();
    void AppendToCECurves();
    std::pair<float, float> GetMinMaxFromFilm(SelectedChannel c);

    Camera camera;
    Film film;
    const bool isMultiChannel;
    Vector2i resolution;
    Primitive scene;
    openpgl::cpp::Field* field;
    const int spp;
    int waveStart;
    std::function<void(int waveStart)> renderWave;
    std::function<void(int waveEnd)> postprocessWave;
    std::function<void(int waveEnd)> saveImage;
    // CPU framebuffer for display, written by the render thread, read by the GUI thread.
    struct {
        RGB *radiance = nullptr;
        RGB *cacheID = nullptr;
        float *fluence = nullptr;
        float *ce = nullptr;
    } cpuFramebuffer;

    int tabHeight = 0;
    constexpr static int inspectorWidth = 400, statusBarHeight = 30;
    Vector2i windowSize;

    void* controlButtonTexID = nullptr;
    int controlButtonTexWidth = 0, controlButtonTexHeight = 0;
    void* renderingTexID = nullptr;

    std::mutex mtxCommand, mtxCPUFramebuffer, mtxCECurves;
    std::condition_variable cv;
    GUICommand command = None;
    RendererState renderState = Initial;
    bool shouldUpdateGPUFramebuffer = false;
    bool autoPlayed = false;
    int forwardWaves = 1;
    bool rayCastingNodeOpened = false;
    bool cacheCurvesNodeOpened = false;
    bool enableRayCasting = false;

    struct {
        Point2i pixel;  // pixel coordinate at the mouse position
        bool valid;   // if the hit is valid
        Point3f hit;  // hit point in world space
        Normal3f normal;
        Point2f uv;
        uint32_t cacheId;
        float fluence;
        float ce;
    } rcData;

    struct {
        float scale = 1.0f;
        float offset = 0.0f;
        bool tonemapped = false;
    } shaderData[Channel_Count];

    SelectedChannel selectedChannel = Channel_Radiance;
    CMaps selectedCMap = CMap_Viridis;

    typedef Vector2f PlotDataEntry;
    struct PlotData {
        bool active;  // should collect data and be plotted
        int order;  // order of the curve created
        Point2f mousePos;  // mouse position when the probe is created
        std::vector<PlotDataEntry> data;
    };
    std::map<uint32_t, PlotData> ceCurves;  // from cache ID to CE curve
};

}

#endif //GUIDINGVIEWER_H
