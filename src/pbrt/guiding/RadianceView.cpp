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
    for (int i = 0; i < PGL_SIGNATURE_MAX_SIZE; ++i)
        m_basisBuffer[i].resize(m_resolution.x * m_resolution.y);
    glGenTextures(1, &m_renderingTex);
    glGenTextures(PGL_SIGNATURE_MAX_SIZE, m_basisTex);
    FilmBaseParameters fp(m_resolution, Bounds2i({0, 0}, m_resolution), filter, 35., sensor,
                          "RadianceView-temp.exr");
    m_cbp.film = {new RGBFilm(fp, RGBColorSpace::sRGB)};
    auto &settings = m_parent->GetIntegratorSettings();
    m_maxDepth = settings.maxDepth - 1;
}

RadianceView::~RadianceView() {
    glDeleteTextures(1, &m_renderingTex);
    glDeleteTextures(PGL_SIGNATURE_MAX_SIZE, m_basisTex);
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
    UpdateBasisBuffer();
}

static pgl_vec2f dir_to_spherical(const pgl_direction &dir) {
    auto cartesian = pgl_vec3f(dir);
    float theta = std::acos(cartesian.z);
    float phi = std::atan2(cartesian.y, cartesian.x);
    if (phi < 0) phi += 2 * M_PI;
    return {theta / M_PIf, phi / (2 * M_PIf)};
}

//A pseudorandom number generator with a seed consisting of 3 uints
static uint32_t pcg_3d(uint32_t x, uint32_t y, uint32_t z) {
    x ^= 12312u;
    // Taken from: https://www.shadertoy.com/view/XlGcRh
    x = x * 1664525u + 1013904223u;
    y = y * 1664525u + 1013904223u;
    z = z * 1664525u + 1013904223u;
    x += y * z;
    y += z * x;
    z += x * y;
    x ^= x >> 16u;
    y ^= y >> 16u;
    z ^= z >> 16u;
    x += y * z;
    y += z * x;
    z += x * y;
    return x;
}

static uint32_t pcg_2d(uint32_t x, uint32_t y) {
    x = x * 1664525u + 1013904223u;
    y = y * 1664525u + 1013904223u;

    x += y * 1664525u;
    y += x * 1664525u;

    x = x ^ (x>>16u);
    y = y ^ (y>>16u);

    x += y * 1664525u;
    y += x * 1664525u;

    x = x ^ (x>>16u);
    y = y ^ (y>>16u);

    return x;
}

// Inserts one 0-bit between any two of the 16 low bits of x
static uint32_t part_1_by_1(uint32_t x) {
    // x = ---- ---- ---- ---- fedc ba98 7654 3210
    x &= 0xffffu;
    // x = ---- ---- fedc ba98 ---- ---- 7654 3210
    x = (x ^ (x << 8u)) & 0xff00ffu;
    // x = ---- fedc ---- ba98 ---- 7654 ---- 3210
    x = (x ^ (x << 4u)) & 0xf0f0f0fu;
    // x = --fe --dc --ba --98 --76 --54 --32 --10
    x = (x ^ (x << 2u)) & 0x33333333u;
    // x = -f-e -d-c -b-a -9-8 -7-6 -5-4 -3-2 -1-0
    x = (x ^ (x << 1u)) & 0x55555555u;
    return x;
}


// Creates a Morton code from two 8-bit integers (costs more than morton_8())
static uint32_t morton_16(uint32_t x, uint32_t y) {
    return (part_1_by_1(y) << 1u) ^ part_1_by_1(x);
}


// Inverse of xi() for max_depth = 16 (or maybe not)
uint32_t invert_xi_16(uint32_t x, uint32_t y) {
    x ^= 2631929843u, y ^= 3492732422u;
    uint32_t z = morton_16(x >> 16u, y >> 16u);
    constexpr uint32_t U[4] = {0u, 1790330939u, 2934368918u, 3293618861u};
    uint32_t seq_no = 0u;
    for (uint32_t bit = 0u; bit < 32u; bit += 2u)
        seq_no ^= U[(z >> (30u - bit)) & 3u] << bit;
    return seq_no;
}


// Inverse of xi() (or maybe not)
uint32_t invert_xi(uint32_t x, uint32_t y, uint32_t max_depth) {
    uint32_t mask = (1u << (2u * max_depth)) - 1u;
    mask = (max_depth == 16u) ? 0xffffffffu : mask;
    return invert_xi_16(x << (32u - max_depth), y << (32u - max_depth)) & mask;
}

// Maps a texel index and an octave index to a bin index. Only the octave least
// significant bits of the texel index should be non-zero. octave must be 8 or
// less.
uint32_t get_bin(uint32_t x, uint32_t y, uint32_t octave, uint32_t log2_bin_count) {
    uint32_t seq_no = invert_xi(x, y, 2u * octave);
    uint32_t bin = (seq_no >> (4u * octave - log2_bin_count)) & ((1u << log2_bin_count) - 1u);
    return bin;
}

static float mix(float a, float b, float t) {
    return (1 - t) * a + t * b;
}

static float fract(float x) {
    return x - std::floor(x);
}

// Implements wrapping of k-bit indices in a way that is compatible with
// octahedral maps
inline static void wrap(uint32_t &x, uint32_t &y, uint32_t res) {
    const uint32_t hres = res >> 1;
    if (x > hres && (y == 0u || y == res)) {
        x = res - x;
    }
    if (y > hres && (x == 0u || x == res)) {
        y = res - y;
    }
    x &= res - 1u, y &= res - 1u;
}

// Wrapping for splatting
inline static void wrap_splat(int &x, int &y, uint32_t res) {
    if (x < 0 || x >= res) {
        x = std::clamp(x, 0, (int)res - 1);
        y = res - 1 - y;
    }
    if (y < 0 || y >= res) {
        y = std::clamp(y, 0, (int)res - 1);
        x = res - 1 - x;
    }
}

void RadianceView::UpdateBasisBuffer() {
    Bounds2i pixelBounds = m_camera->GetFilm().PixelBounds();
    auto frame = Frame::FromZ(m_prev.normal);
    const auto contribType = m_parent->GetSubdivCfg().contribType;
    const uint8_t S = pglGetSignatureSize();
    const uint8_t halfS = S >> 1;
    const uint8_t log2_bin_count = (uint8_t) std::log2(S);
    if (contribType == PGL_SPATIAL_CONTRIB_BASIS_XI) {
        if (S != (1 << log2_bin_count)) {
            std::cerr << "Signature size must be a power of 2" << std::endl;
            return;
        }
    }
    const uint32_t oct_res = pglGetOctahedralResolution();
    const uint8_t octave_min = pglGetOctaveMin(), octave_max = pglGetOctaveMax();
    const float sigma = pglGetSplatSigma();
    // const float basis_normalizer = pow(2.0, 1.0 - float(octave_min)) - pow(0.5, float(octave_max));
    const float gamma = pglGetOctaveGamma();
    const float alpha = -0.5f / (sigma*sigma);
    const float kernel_lb = std::exp(alpha);
    for (Point2i p: pixelBounds) {
        float theta = m_stepTheta * (0.5f + float(p.y));
        float phi = m_stepPhi * (0.5f + float(p.x));

        Vector3f dir = SphericalDirection(std::sin(theta), std::cos(theta), phi);
        if (m_localFrame)
            dir = frame.FromLocal(dir);
        pgl_direction pglDir = pgl_vec3f{dir.x, dir.y, dir.z};
        const size_t pixel_index = p.y * m_resolution.x + p.x;

        if (contribType == PGL_SPATIAL_CONTRIB_BASIS || contribType == PGL_SPATIAL_CONTRIB_BASIS_XI) {
            // Find octahedral map coordinate
            auto uv = pgl_vec2f(pglDir);  // [-1, 1]
            uv.x = uv.x * 0.5 + 0.5;
            uv.y = uv.y * 0.5 + 0.5;   // to [0, 1]

            // uv = {float(p.x) / m_resolution.x, float(p.y) / m_resolution.y};  // debug

            // * Evaluates all basis functions at the given coordinate
            for (uint8_t j = 0; j < S; ++j)
                m_basisBuffer[j][pixel_index] = 0.0;
            float normalizer = 0.0;
            // Iterate over all octaves
            for (uint8_t k = octave_min; k <= octave_max; ++k) {
                const uint32_t res = 1 << k;
                const float weight = pow(gamma, float(k));
                normalizer += weight;
                // Discretize uv at the appropriate resolution
                pgl_vec2f octave_uv = {uv.x * float(res), uv.y * float(res)};
                uint32_t x00 = uint32_t(octave_uv.x), y00 = uint32_t(octave_uv.y);
                // Generate offsets
                uint32_t x01 = x00, y01 = y00 + 1;
                uint32_t x10 = x00 + 1, y10 = y00;
                uint32_t x11 = x00 + 1, y11 = y00 + 1;
                // Apply wrapping to ensure continuity on the sphere domain
                wrap(x00, y00, res);
                wrap(x01, y01, res);
                wrap(x10, y10, res);
                wrap(x11, y11, res);
                uint8_t h00;
                uint8_t h01;
                uint8_t h10;
                uint8_t h11;
                if (contribType == PGL_SPATIAL_CONTRIB_BASIS) {
                    h00 = pcg_3d(x00, y00, k) % S;
                    h01 = pcg_3d(x01, y01, k) % S;
                    h10 = pcg_3d(x10, y10, k) % S;
                    h11 = pcg_3d(x11, y11, k) % S;
                } else {
                    // using Xi-seq for lower discrepancy and less clumping
                    h00 = get_bin(x00, y00, k, log2_bin_count);
                    h01 = get_bin(x01, y01, k, log2_bin_count);
                    h10 = get_bin(x10, y10, k, log2_bin_count);
                    h11 = get_bin(x11, y11, k, log2_bin_count);
                }

                for (uint8_t j = 0; j < S; ++j) {
                    // Determine whether this bin gets the sample
                    float M00 = (h00 == j) ? 1.0 : 0.0;
                    float M01 = (h01 == j) ? 1.0 : 0.0;
                    float M10 = (h10 == j) ? 1.0 : 0.0;
                    float M11 = (h11 == j) ? 1.0 : 0.0;
                    // Perform bilinear interpolation
                    float M0 = mix(M00, M01, fract(octave_uv.y));
                    float M1 = mix(M10, M11, fract(octave_uv.y));
                    float M = mix(M0, M1, fract(octave_uv.x));
                    // Accumulate into the result
                    m_basisBuffer[j][pixel_index] += weight * M;
                }
            }

            for (uint8_t j = 0; j < S; ++j) {
                m_basisBuffer[j][pixel_index] /= normalizer;
            }
            // // Check sum
            // float sum = 0;
            // for (uint8_t j = 0; j < S; ++j)
            //     sum += m_basisBuffer[j][pixel_index];
            // CHECK(std::abs(sum - 1.0) < 1e-5);
        } else if (contribType == PGL_SPATIAL_CONTRIB_LATITUDE_LONGITUDE) {
            auto uv = dir_to_spherical(pglDir);

            float u = fract(uv.x * oct_res);  // latitude
            float v = fract(uv.y * 2 * oct_res);  // longitude

            for (uint8_t j = 0; j < S; ++j) {
                float x = M_PI_2f * (float(halfS) * (j < halfS ? u : v) - float(j < halfS ? j : j - halfS));
                float b = 0.0;
                if ((-M_PI_2f <= x && x < M_PI_2f) || (-M_PI_2f <= x - M_PI_2f * halfS && x - M_PI_2f * halfS < M_PI_2f)) {
                    b = std::cos(x);
                    b *= b;
                }
                m_basisBuffer[j][pixel_index] = b;
            }
        } else if (contribType == PGL_SPATIAL_CONTRIB_SPLAT) {
            // Find octahedral map coordinate
            auto uv = pgl_vec2f(pglDir);  // [-1, 1]
            uv.x = uv.x * 0.5 + 0.5;
            uv.y = uv.y * 0.5 + 0.5;   // to [0, 1]

            // Splatting
            // 3x3 Gaussian kernel
            constexpr pgl_vec2i offsets[9] = {
                {-1, -1}, {0, -1}, {+1, -1},
                {-1,  0}, {0,  0}, {+1,  0},
                {-1, +1}, {0, +1}, {+1, +1}
            };

            pgl_vec2i pi{
                std::clamp((int)(uv.x * oct_res), 0, (int)oct_res - 1),
                std::clamp((int)(uv.y * oct_res), 0, (int)oct_res - 1)
            };  // {0, .., oct_res-1}

            // Dynamically compute kernel weights of each neighbor's center
            for (uint8_t j = 0; j < S; ++j)
                m_basisBuffer[j][pixel_index] = 0.0;

            float sumCoeff = 0;
            for (int i = 0; i < 9; ++i) {
                pgl_vec2i qi = {pi.x + offsets[i].x, pi.y + offsets[i].y};
                // pgl_vec2f delta = {(float)(pi.x - qi.x), (float)(pi.y - qi.y)}; // old approach: static weights
                pgl_vec2f delta = {uv.x * oct_res - (qi.x + 0.5f), uv.y * oct_res - (qi.y + 0.5f)};
                float coeff = std::max(std::exp(alpha * (delta.x*delta.x + delta.y*delta.y)) - kernel_lb, 0.0f);
                sumCoeff += coeff;
                
                // uint8_t j = pcg_2d(qi.x, qi.y) % S;  // hash to bin
                wrap_splat(qi.x, qi.y, oct_res);
                uint8_t j = pcg_2d(qi.x, qi.y) % S;  // hash to bin
                m_basisBuffer[j][pixel_index] += coeff;
            }

            // Normalize weights
            for (uint8_t j = 0; j < S; ++j)
                m_basisBuffer[j][pixel_index] /= sumCoeff;
        } else {  // NN
            for (uint8_t j = 0; j < S; ++j)
                m_basisBuffer[j][pixel_index] = 0.0;
            m_basisBuffer[pglGetSignatureIndex(pglDir)][pixel_index] = 1.0;
        }
    }

    for (uint8_t j = 0; j < S; ++j)
        UpdateTextureFromFloatData((GLuint) (uintptr_t) m_basisTex[j], m_basisBuffer[j].data(), m_resolution.x, m_resolution.y, false);
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
            for (size_t i = 0; i < pglGetSignatureSize(); i++) {
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
    for (int i = 0; i < PGL_SIGNATURE_MAX_SIZE; ++i)
        m_basisBuffer[i].resize(m_resolution.x * m_resolution.y);
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

Image RadianceView::GetImage() const {
    CHECK(!IsRendering());
    Image image(PixelFormat::Float, m_resolution, {"R", "G", "B"});

    ParallelFor2D(Bounds2i({0, 0}, m_resolution), [&](Point2i p) {
        RGB rgb = m_cpuBuffer[p.y * m_resolution.x + p.x];

        if (std::max({rgb.r, rgb.g, rgb.b}) > 65504) {  // Clamping
            if (rgb.r > 65504)
                rgb.r = 65504;
            if (rgb.g > 65504)
                rgb.g = 65504;
            if (rgb.b > 65504)
                rgb.b = 65504;
        }

        image.SetChannels(p, {rgb[0], rgb[1], rgb[2]});
    });

    return image;
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
        if (float cosineTerm = Dot(d, m_prev.normal); cosineTerm < 0) {
            m_cpuBuffer[index] = RGB(0, 0, 0);
        } else {
            float val = RGBToScalar(m_cpuBuffer[index]);
            float cosTheta;
            if (m_localFrame) cosTheta = Clamp(cosineTerm, -1, 1);
            else cosTheta = d.z;
            float sinTheta = std::sqrt(1 - cosTheta * cosTheta);
            thread_normalizer += val * sinTheta * m_stepPhi * m_stepTheta;
            for (uint8_t j = 0; j < pglGetSignatureSize(); ++j) {
                thread_signature.signature[j] += val * sinTheta * m_stepPhi * m_stepTheta * m_basisBuffer[j][index] * (m_parent->GetSubdivCfg().multiplyCosine ? cosineTerm : 1.0f);
            }
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
        glBindTexture(GL_TEXTURE_2D, m_basisTex[m_selectedBinIndex]);
        shader.setUniform1i("basis_map", 1);
        shader.setUniform1ui("selected_bin_index", m_selectedBinIndex);

        // Render!
        m_overlayFramebuffer.draw();
        m_overlayFramebuffer.unbind();
    }
}

void RadianceView::Draw() {
    ImGui::TextDisabled("(?)");
    ImGui::SameLine();
    ImGui::SetItemTooltip("The radiance view will automatically render when there is left click on the viewport and the render thread is not busy.");
    ImGui::ProgressBar((float) m_numSamples / (float) m_spp, {ImGui::GetContentRegionAvail().x, 0}, m_numSamples >= m_spp ? StringPrintf("%d SPP Done", m_spp).c_str() : StringPrintf("%d/%d SPP", m_numSamples, m_spp).c_str());
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
    auto oldSpp = m_spp;
    if (ImGui::DragInt("SPP", &m_spp, 0.2, 1, 1024)) {
        if (m_spp < oldSpp) needsRestart = true;
    }
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
            // Select the bin index with the largest basis function value
            uint8_t jmax = PGL_SIGNATURE_MAX_SIZE;
            float maxVal = 0;
            for (uint8_t j = 0; j < pglGetSignatureSize(); ++j) {
                if (m_basisBuffer[j][idx] > maxVal) {
                    maxVal = m_basisBuffer[j][idx];
                    jmax = j;
                }
            }
            SetSelectedBinIndex(jmax);
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ResetSelectedBinIndex();
        }
    }
}