//
// Created by fengshi on 10/23/24.
//

#include "RadianceView.h"
#include "Application.h"

using namespace pbrt;

static BoxFilter *filter = new BoxFilter(Vector2f{0.5, 0.5});
static PixelSensor *sensor = PixelSensor::CreateDefault();

RadianceView::RadianceView(pbrt::Application *parent, const pbrt::Primitive &scene,
                           const std::vector<pbrt::Light> &lights)
    : View(parent), m_localFrame(parent->sdrLocalFrame), m_exposure(parent->sdrExposure),
      m_scene(scene), m_lights(lights),
      m_framebuffer(m_resolution.x, m_resolution.y, PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag"),
      m_overlayFramebuffer(m_resolution.x, m_resolution.y, PBRT_ROOT_DIR "src/pbrt/shaders/overlay_bin_index.frag") {
    m_stepPhi = (2.0f * M_PI) / (float) m_resolution.x;
    m_stepTheta = (M_PI) / (float) m_resolution.y;
    m_cpuBuffer.resize(m_resolution.x * m_resolution.y);
    m_binIndexBuffer.resize(m_resolution.x * m_resolution.y);
    glGenTextures(1, &m_renderingTex);
    glGenTextures(1, &m_binIndexTex);
    FilmBaseParameters fp(m_resolution, Bounds2i({0, 0}, m_resolution), filter, 35., sensor,
                          "RadianceView-temp.exr");
    m_cbp.film = {new RGBFilm(fp, RGBColorSpace::sRGB)};
    auto &settings = m_parent->GetIntegratorSettings();
    m_maxDepth = settings.maxDepth - 1;
}

RadianceView::~RadianceView() {
    glDeleteTextures(1, &m_renderingTex);
    glDeleteTextures(1, &m_binIndexTex);
    auto film = m_cbp.film.Cast<RGBFilm>();
    delete film;
}

void RadianceView::RenderStart(const pbrt::Point3f &pos, const pbrt::Normal3f &normal) {
    m_prev = {true, pos, normal, m_localFrame};
    RenderStart();
}

void RadianceView::RenderStart() {
    auto pos = m_prev.pos;
    auto normal = Vector3f(m_prev.normal);
    std::fill(m_cpuBuffer.begin(), m_cpuBuffer.end(), RGB(0, 0, 0));
    integratedSignature = {};
    m_cpuBufferUpdated = true;
    m_numSamples = 0;
    // Setup sampler, camera and integrator
    m_sampler = std::make_unique<IndependentSampler>(m_spp);
    auto eye = pos + m_rayEps * normal;
    Transform transform = Translate(Vector3f(eye));
    m_frame = Frame::FromZ(normal);
    m_cbp.cameraTransform = CameraTransform(AnimatedTransform(transform));
    m_camera = std::make_unique<SphericalCamera>(m_cbp, SphericalCamera::Mapping::EquiRectangular);
    auto camera = Camera(m_camera.get());
    auto sampler = Sampler(m_sampler.get());
    m_integrator = std::make_unique<PathIntegrator>(m_maxDepth, camera, sampler, m_scene, m_lights);
    UpdateBinIndexBuffer();
}

void RadianceView::UpdateBinIndexBuffer() {
    Bounds2i pixelBounds = m_camera->GetFilm().PixelBounds();
    auto frame = Frame::FromZ(m_prev.normal);
    for (Point2i p: pixelBounds) {
        float theta = m_stepTheta * (0.5f + float(p.y));
        float phi = m_stepPhi * (0.5f + float(p.x));

        Vector3f dir = SphericalDirection(std::sin(theta), std::cos(theta), phi);
        if (m_localFrame)
            dir = frame.FromLocal(dir);
        pgl_vec3f pglDir{dir.x, dir.y, dir.z};

        size_t index = p.y * m_resolution.x + p.x;
        m_binIndexBuffer[index] = pglGetSignatureIndex(pglDir);
    }

    UpdateTextureFromUInt8Data((GLuint) (uintptr_t) m_binIndexTex, m_binIndexBuffer.data(), m_resolution.x, m_resolution.y, false);
}

thread_local double thread_normalizer = 0;
thread_local PGLDirectionalSignature thread_signature;

void RadianceView::RenderStep() {
    // Render one sample per pixel
    CHECK_LT(m_numSamples, m_spp);
    Bounds2i pixelBounds = m_camera->GetFilm().PixelBounds();
    double normalizer = 0;
    PGLDirectionalSignature signature{};
    std::mutex mutex;
    ParallelFor2D(pixelBounds, [&](Bounds2i tileBounds) {
        // Render image tile given by _tileBounds_
        ScratchBuffer &scratchBuffer = m_scratchBuffers.Get();
        IndependentSampler _sampler = *m_sampler;
        Sampler sampler(&_sampler);
        thread_normalizer = 0;
        thread_signature = {};
        for (Point2i pPixel : tileBounds) {
            // Render samples in pixel _pPixel_
            sampler.StartPixelSample(pPixel, m_numSamples);
            EvaluatePixelSample(pPixel, m_numSamples, sampler, scratchBuffer);
            scratchBuffer.Reset();
        }
        {
            std::lock_guard lock(mutex);
            normalizer += thread_normalizer;
            for (size_t i = 0; i < PGL_SIGNATURE_SIZE; i++) {
                signature.signature[i] += thread_signature.signature[i];
            }
        }
    });
    m_normalizer = normalizer;
    integratedSignature = signature;
    m_numSamples++;
    m_cpuBufferUpdated = true;
}

static inline float RGBToScalar(const RGB &rgb) {
    return std::max(std::max(rgb.r, rgb.g), rgb.b);  // consistent with OpenPGL
    // return Luminance(rgb);
}

double RadianceView::GetPDF(const pbrt::Point2i &p) const {
    return RGBToScalar(m_cpuBuffer[p.y * m_resolution.x + p.x]) / m_normalizer;
}

void RadianceView::SetResolution(const pbrt::Point2i &resolution) {
    m_resolution = resolution;
    m_framebuffer.rescale(resolution.x, resolution.y);
    m_cpuBuffer.resize(m_resolution.x * m_resolution.y);
    m_binIndexBuffer.resize(m_resolution.x * m_resolution.y);
    m_stepPhi = (2.0f * M_PI) / (float) m_resolution.x;
    m_stepTheta = (M_PI) / (float) m_resolution.y;
    auto film = m_cbp.film.Cast<RGBFilm>();
    delete film;
    auto filter = new BoxFilter(Vector2f{0.5, 0.5});
    FilmBaseParameters fp(m_resolution, Bounds2i({0, 0}, m_resolution), filter, 35., sensor,
                          "RadianceView-temp.exr");
    m_cbp.film = {new RGBFilm(fp, RGBColorSpace::sRGB)};

    if (m_prev.valid) {
        RenderStart();
    }
}

void RadianceView::EvaluatePixelSample(pbrt::Point2i pPixel, int sampleIndex, pbrt::Sampler sampler,
                                       pbrt::ScratchBuffer &scratchBuffer) {
    // Sample wavelengths for the ray
    Float lu = sampler.Get1D();
    if (Options->disableWavelengthJitter)
        lu = 0.5;
    SampledWavelengths lambda = m_camera->GetFilm().SampleWavelengths(lu);

    // Initialize _CameraSample_ for current sample
    Filter filter = m_camera->GetFilm().GetFilter();
    CameraSample cameraSample = GetCameraSample(sampler, pPixel, filter);

    // Generate camera ray for current sample
    pstd::optional<CameraRayDifferential> cameraRay =
            m_camera->GenerateRayDifferential(cameraSample, lambda);

    // Trace _cameraRay_ if valid
    SampledSpectrum L(0.);
    if (cameraRay) {
        pstd::swap(cameraRay->ray.d.y, cameraRay->ray.d.z);  // to match SamplingDistributionView
        if (m_localFrame)
            cameraRay->ray.d = m_frame.FromLocal(cameraRay->ray.d);
        // Double check that the ray's direction is normalized.
        DCHECK_GT(Length(cameraRay->ray.d), .999f);
        DCHECK_LT(Length(cameraRay->ray.d), 1.001f);
        // Scale camera ray differentials based on image sampling rate
        Float rayDiffScale =
                std::max<Float>(.125f, 1 / std::sqrt((Float)sampler.SamplesPerPixel()));
        if (!Options->disablePixelJitter)
            cameraRay->ray.ScaleDifferentials(rayDiffScale);

        // Evaluate radiance along camera ray
        L = cameraRay->weight * m_integrator->Li(pPixel, cameraRay->ray, lambda, sampler, scratchBuffer, nullptr);

        // Issue warning if unexpected radiance value is returned
        if (L.HasNaNs()) {
            LOG_ERROR("Not-a-number radiance value returned for pixel (%d, "
                      "%d), sample %d. Setting to black.",
                      pPixel.x, pPixel.y, sampleIndex);
            L = SampledSpectrum(0.f);
        } else if (IsInf(L.y(lambda))) {
            LOG_ERROR("Infinite radiance value returned for pixel (%d, %d), "
                      "sample %d. Setting to black.",
                      pPixel.x, pPixel.y, sampleIndex);
            L = SampledSpectrum(0.f);
        }
    }
    // Add camera ray's contribution to the CPU buffer. The camera film is not used
    size_t index = pPixel.y * m_resolution.x + pPixel.x;
    RGB rgb = m_camera->GetFilm().ToOutputRGB(L, lambda);
    m_cpuBuffer[index] = Lerp(1 / (Float) (sampleIndex + 1), m_cpuBuffer[index], rgb);
    if (cameraRay) {
        auto d = cameraRay->ray.d;
        if (Dot(d, m_prev.normal) < 0) {
            m_cpuBuffer[index] = RGB(0, 0, 0);
        } else {
            float val = RGBToScalar(m_cpuBuffer[index]);
            float cosTheta;
            if (m_localFrame) cosTheta = Clamp(Dot(d, m_prev.normal), -1, 1);
            else cosTheta = d.z;
            float sinTheta = std::sqrt(1 - cosTheta * cosTheta);
            thread_normalizer += val * sinTheta * m_stepPhi * m_stepTheta;
            uint8_t binIdx = m_binIndexBuffer[index];
            thread_signature.signature[binIdx] += val * sinTheta * m_stepPhi * m_stepTheta;
        }
    }
}

void RadianceView::Clear() {
    m_prev.valid = false;
    std::fill(m_cpuBuffer.begin(), m_cpuBuffer.end(), RGB(0, 0, 0));
    integratedSignature = {};
    m_normalizer = 1;
    m_cpuBufferUpdated = true;
}

void RadianceView::UpdateFramebuffer() {
    // Render to the tonemapped framebuffer if the CPU buffer has been updated
    if (m_cpuBufferUpdated.exchange(false)) {
        UpdateTextureFromRGBData((GLuint) (uintptr_t) m_renderingTex, m_cpuBuffer.data(), m_resolution.x, m_resolution.y, true);
    }

    m_framebuffer.bind();
    m_framebuffer.clear();

    {
        Shader &shader = m_framebuffer.getShader();
        shader.bind();
        Colormap cmap = m_pdf ? CMap_Viridis : CMap_None;
        ConfigureTonemapShader(shader, m_renderingTex, false, {
                                   m_pdf ? (float) (m_exposure / m_normalizer) : m_exposure, 0.0f, std::numeric_limits<float>::infinity(),
                                   cmap_tex_ids[cmap]
                               });
    }

    // Render!
    m_framebuffer.draw();
    m_framebuffer.unbind();

    if (HasSelectedBinIndex()) {
        // Second pass: overlay with the bin index map
        m_overlayFramebuffer.bind();
        m_overlayFramebuffer.clear();

        Shader &shader = m_overlayFramebuffer.getShader();
        shader.bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_framebuffer.getTexture());
        shader.setUniform1i("image_tex", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_binIndexTex);
        shader.setUniform1i("index_map", 1);
        shader.setUniform1ui("selected_index", m_selectedBinIndex);

        // Render!
        m_overlayFramebuffer.draw();
        m_overlayFramebuffer.unbind();
    }
}

void RadianceView::Draw() {
    ImGui::TextDisabled("(?)");
    ImGui::SameLine();
    ImGui::SetItemTooltip("The radiance view will automatically render when there is left click on the viewport and the render thread is not busy.");
    ImGui::ProgressBar((float) m_numSamples / (float) m_spp, {ImGui::GetContentRegionAvail().x, 0}, m_numSamples >= m_spp ? "Done" : StringPrintf("%d/%d SPP", m_numSamples, m_spp).c_str());
    bool needsRestart = false;
    needsRestart |= ImGui::Checkbox("Local Frame", &m_localFrame);
    needsRestart |= m_localFrame != m_prev.localFrame;
    m_prev.localFrame = m_localFrame;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90);
    ImGui::DragFloat("Exposure", &m_exposure, 0.01f, 0, 0, "%.4f");
    m_exposure = std::max(m_exposure, 0.0f);
    ImGui::Checkbox("PDF", &m_pdf);
    ImGui::SetItemTooltip("Normalize the radiance to the ground truth distribution.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(30);
    needsRestart |= ImGui::DragInt("SPP", &m_spp, 0.2, 1, 1024);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(30);
    needsRestart |= ImGui::DragInt("Max Depth", &m_maxDepth, 0.1, 0, 128);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60);
    needsRestart |= ImGui::DragFloat("Offset", &m_rayEps, 0.0001, 0, 1, "%.1e");

    if (needsRestart && m_prev.valid) {
        RenderStart();
    }
    UpdateFramebuffer();

    ImVec2 size{(float) m_resolution.x, (float) m_resolution.y};
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // Scale the image to fit the available space
    float scale = std::min(avail.x / size.x, avail.y / size.y);
    size = {size.x * scale, size.y * scale};
    ImVec2 offset{(avail.x - size.x) / 2, (avail.y - size.y) / 2};
    ImVec2 current = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos({current.x + offset.x, current.y + offset.y});
    auto leftTop = ImGui::GetCursorScreenPos();
    GLuint tex = HasSelectedBinIndex() ? m_overlayFramebuffer.getTexture() : m_framebuffer.getTexture();
    ImGui::Image((ImTextureID) (uintptr_t) tex, size);

    // Hovering: show value at the pixel
    if (ImGui::IsItemHovered()) {
        auto pos = ImGui::GetMousePos();
        Point2i pixel((int) ((pos.x - leftTop.x) / scale), (int) ((pos.y - leftTop.y) / scale));
        int idx = pixel.y * m_resolution.x + pixel.x;
        if (ImGui::BeginTooltip()) {
            float theta = m_stepTheta * (0.5f + float(pixel.y));
            float phi = m_stepPhi * (0.5f + float(pixel.x));
            ImGui::Text("Omega: (%.1f, %.1f) deg", Degrees(theta), Degrees(phi));
            RGB rgb = m_cpuBuffer[idx];
            if (m_pdf) ImGui::Text("PDF: %.6lf", RGBToScalar(rgb) / m_normalizer);
            else ImGui::Text("Li: (%.4f, %.4f, %.4f)", rgb.r, rgb.g, rgb.b);
            ImGui::EndTooltip();
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left, true)) {
            // Show the selected bin index
            SetSelectedBinIndex(m_binIndexBuffer[idx]);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ResetSelectedBinIndex();
        }
    }
}