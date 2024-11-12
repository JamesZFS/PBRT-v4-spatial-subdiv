//
// Created by fengshi on 10/23/24.
//

#include "RadianceView.h"
#include "Application.h"

using namespace pbrt;


RadianceView::RadianceView(pbrt::Application *parent, const pbrt::Primitive &scene,
                           const std::vector<pbrt::Light> &lights)
    : View(parent), m_localFrame(parent->sdrLocalFrame), m_exposure(parent->sdrExposure),
      m_scene(scene), m_lights(lights),
      m_framebuffer(m_resolution.x, m_resolution.y, PBRT_ROOT_DIR "src/pbrt/shaders/image_tonemapped.frag") {
    m_stepPhi = (2.0f * M_PI) / (float) m_resolution.x;
    m_stepTheta = (M_PI) / (float) m_resolution.y;
    m_cpuBuffer.resize(m_resolution.x * m_resolution.y);
    glGenTextures(1, &m_renderingTex);
    auto filter = new BoxFilter(Vector2f{0.5, 0.5});
    FilmBaseParameters fp(m_resolution, Bounds2i({0, 0}, m_resolution), filter, 35., PixelSensor::CreateDefault(),
                          "RadianceView-temp.exr");
    m_cbp.film = {new RGBFilm(fp, RGBColorSpace::sRGB)};
}

RadianceView::~RadianceView() {
    glDeleteTextures(1, &m_renderingTex);
    auto film = m_cbp.film.Cast<RGBFilm>();
    delete film;
}

void RadianceView::RenderStart(const pbrt::Point3f &pos, const pbrt::Normal3f &normal) {
    m_prev = {true, pos, normal};
    RenderStart();
}

void RadianceView::RenderStart() {
    auto pos = m_prev.pos;
    auto normal = Vector3f(m_prev.normal);
    std::fill(m_cpuBuffer.begin(), m_cpuBuffer.end(), RGB(0, 0, 0));
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
    auto &settings = m_parent->GetIntegratorSettings();
    m_integrator = std::make_unique<PathIntegrator>(settings.maxDepth - 1, camera, sampler, m_scene, m_lights);
}

thread_local double thread_normalizer = 0;

void RadianceView::RenderStep() {
    // Render one sample per pixel
    CHECK_LT(m_numSamples, m_spp);
    Bounds2i pixelBounds = m_camera->GetFilm().PixelBounds();
    double normalizer = 0;
    std::mutex mutex;
    ParallelFor2D(pixelBounds, [&](Bounds2i tileBounds) {
        // Render image tile given by _tileBounds_
        ScratchBuffer &scratchBuffer = m_scratchBuffers.Get();
        IndependentSampler _sampler = *m_sampler;
        Sampler sampler(&_sampler);
        thread_normalizer = 0;
        for (Point2i pPixel : tileBounds) {
            // Render samples in pixel _pPixel_
            sampler.StartPixelSample(pPixel, m_numSamples);
            EvaluatePixelSample(pPixel, m_numSamples, sampler, scratchBuffer);
            scratchBuffer.Reset();
        }
        {
            std::lock_guard lock(mutex);
            normalizer += thread_normalizer;
        }
    });
    m_normalizer = normalizer;
    m_numSamples++;
    m_cpuBufferUpdated = true;
}

double RadianceView::GetPDF(const pbrt::Point2i &p) const {
    return Luminance(m_cpuBuffer[p.y * m_resolution.x + p.x]) / m_normalizer;
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
            float val = Luminance(m_cpuBuffer[index]);
            float cosTheta;
            if (m_localFrame) cosTheta = Clamp(Dot(d, m_prev.normal), -1, 1);
            else cosTheta = d.z;
            float sinTheta = std::sqrt(1 - cosTheta * cosTheta);
            thread_normalizer += val * sinTheta * m_stepPhi * m_stepTheta;
        }
    }
}

void RadianceView::Clear() {
    m_prev.valid = false;
    std::fill(m_cpuBuffer.begin(), m_cpuBuffer.end(), RGB(0, 0, 0));
    m_cpuBufferUpdated = true;
}

void RadianceView::UpdateFramebuffer() {
    // Render to the tonemapped framebuffer if the CPU buffer has been updated
    if (m_cpuBufferUpdated.exchange(false)) {
        UpdateTextureFromRGBData((GLuint) (uintptr_t) m_renderingTex, m_cpuBuffer.data(), m_resolution.x, m_resolution.y, true);
    }

    m_framebuffer.bind();
    m_framebuffer.clear();

    Shader &shader = m_framebuffer.getShader();
    shader.bind();
    Colormap cmap = m_pdf ? CMap_Viridis : CMap_None;
    ConfigureTonemapShader(shader, m_renderingTex, false, {
                               m_pdf ? (float) (m_exposure / m_normalizer) : m_exposure, 0.0f, std::numeric_limits<float>::infinity(),
                               cmap_tex_ids[cmap]
                           });

    // Render!
    m_framebuffer.draw();
    m_framebuffer.unbind();
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
    ImGui::SetNextItemWidth(50);
    needsRestart |= ImGui::DragInt("SPP", &m_spp, 0.2, 1, 1024);
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
    ImGui::Image((ImTextureID) (uintptr_t) m_framebuffer.getTexture(), size);

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
            if (m_pdf) ImGui::Text("PDF: %.6lf", Luminance(rgb) / m_normalizer);
            else ImGui::Text("Li: (%.4f, %.4f, %.4f)", rgb.r, rgb.g, rgb.b);
            ImGui::EndTooltip();
        }
    }
}