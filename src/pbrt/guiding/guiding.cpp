//
// Created by fengshi on 9/12/24.
//

#include <pbrt/cpu/integrators.h>

#include <pbrt/bsdf.h>
#include <pbrt/bssrdf.h>
#include <pbrt/cameras.h>
#include <pbrt/film.h>
#include <pbrt/filters.h>
#include <pbrt/interaction.h>
#include <pbrt/lights.h>
#include <pbrt/materials.h>
#include <pbrt/media.h>
#include <pbrt/options.h>
#include <pbrt/paramdict.h>
#include <pbrt/samplers.h>
#include <pbrt/shapes.h>
#include <pbrt/util/bluenoise.h>
#include <pbrt/util/check.h>
#include <pbrt/util/color.h>
#include <pbrt/util/colorspace.h>
#include <pbrt/util/containers.h>
#include <pbrt/util/display.h>
#include <pbrt/util/error.h>
#include <pbrt/util/file.h>
#include <pbrt/util/hash.h>
#include <pbrt/util/image.h>
#include <pbrt/util/lowdiscrepancy.h>
#include <pbrt/util/math.h>
#include <pbrt/util/memory.h>
#include <pbrt/util/parallel.h>
#include <pbrt/util/print.h>
#include <pbrt/util/progressreporter.h>
#include <pbrt/util/pstd.h>
#include <pbrt/util/rng.h>
#include <pbrt/util/sampling.h>
#include <pbrt/util/spectrum.h>
#include <pbrt/util/stats.h>
#include <pbrt/util/string.h>

#include <algorithm>
#include <iostream>

#include <pbrt/guiding/guiding.h>
#include <pbrt/guiding/Application.h>

namespace pbrt {

STAT_PERCENT("Integrator/Zero-radiance paths", zeroRadiancePaths, totalPaths);
STAT_PERCENT("Integrator/Regularized BSDFs", regularizedBSDFs, totalBSDFs);
STAT_INT_DISTRIBUTION("Integrator/Path length", pathLength);
STAT_COUNTER("Integrator/Volume interactions", volumeInteractions);
STAT_COUNTER("Integrator/Surface interactions", surfaceInteractions);

STAT_TIME_COUNTER("Pure Rendering Time", pureRenderingTime);
STAT_TIME_COUNTER("Guiding Cache Training", guidingCacheUpdateTime);
STAT_TIME_COUNTER("Image-space Guiding Buffer Training", imageSpaceGudingBufferUpdateTime);

// GuidedPathIntegrator Method Definitions
GuidedPathIntegrator::GuidedPathIntegrator(const int maxDepth, const int minRRDepth, const bool useNEE, const GuidingSettings guideSettings, const RGBColorSpace *colorSpace, Camera camera, Sampler sampler,
                               Primitive aggregate, std::vector<Light> lights,
                               const std::string &lightSampleStrategy, bool regularize)
    : RayIntegrator(camera, sampler, aggregate, lights),
      settings{maxDepth, minRRDepth, useNEE, regularize},
      guideSettings(guideSettings),
      colorSpace(colorSpace),
      lightSampler(LightSampler::Create(lightSampleStrategy, lights, Allocator())) {
            std::cout<< "GuidedPathIntegrator:" <<std::endl;
            std::cout<< "\t maxDepth = " << maxDepth << std::endl;
            std::cout<< "\t minRRDepth = " << minRRDepth << std::endl;
            std::cout<< "\t useNEE = " << useNEE << std::endl;
            std::cout<< "\t enableGuiding = " << guideSettings.enableGuiding << std::endl;
            std::cout<< "\t surfaceGuidingType = " << guideSettings.surfaceGuidingType << std::endl;
            std::cout<< "\t loadGuidingCache = " << guideSettings.loadGuidingCache << std::endl;
            std::cout<< "\t storeGuidingCache = " << guideSettings.storeGuidingCache << std::endl;
            std::cout<< "\t guidingCacheFileName = " << guideSettings.guidingCacheFileName << std::endl;
            std::cout<< "\t lightSampleStrategy = " << lightSampleStrategy << std::endl;
            std::cout<< "\t regularize = " << regularize << std::endl;
        guiding_device = new openpgl::cpp::Device(PGL_DEVICE_TYPE_CPU_4);
        guiding_fieldConfig.Init(PGL_SPATIAL_STRUCTURE_KDTREE, guideSettings.dtype, true,
            guideSettings.treesamplecountthreshold, guideSettings.treeminsamplescandidatesplit, guideSettings.treemaxdepth);
        guiding_fieldSubdivConfig = *(PGLKDTreeArguments*) guiding_fieldConfig.GetSubdivConfig();
        guiding_fieldSubdivConfig.deterministic = guideSettings.deterministic;
        guiding_fieldSubdivConfig.initializingIters = guideSettings.treeinitializingiters;
        guiding_fieldSubdivConfig.forcedSampleCountThreshold = (uint32_t) guideSettings.treeforcedsamplecountthreshold;
        guiding_fieldSubdivConfig.lookaheadDepth = guideSettings.treelookaheaddepth;
        guiding_fieldSubdivConfig.minSamplesPromotion = guideSettings.treeminsamplespromotion;
        guiding_fieldSubdivConfig.minSamplesCandidateSplit = guideSettings.treeminsamplescandidatesplit;
        guiding_fieldSubdivConfig.signatureDistanceThreshold = guideSettings.treeadaptivethreshold;
        guiding_fieldSubdivConfig.stdMultiplier = guideSettings.treestdmultiplier;
        guiding_fieldSubdivConfig.filterType = guideSettings.treefiltertype;
        guiding_fieldSubdivConfig.inlierPercent = guideSettings.treeinlierpercent;
        guiding_fieldSubdivConfig.DBORstdMultiplier = guideSettings.treedborstdmultiplier;
        guiding_fieldSubdivConfig.tEpsK = guideSettings.treetepsk;
        guiding_fieldSubdivConfig.ceDecay = guideSettings.treecedecay;
        guiding_fieldSubdivConfig.enablePromotion = guideSettings.treeenablepromotion;
        guiding_fieldSubdivConfig.multiplyCosine = guideSettings.treemultiplycosine;
        guiding_fieldSubdivConfig.reproject = guideSettings.treereproject;
        guiding_fieldSubdivConfig.nonRecursive = guideSettings.treenonrecursive;
        guiding_fieldSubdivConfig.singlePromotion = guideSettings.treesinglepromotion;
        guiding_fieldSubdivConfig.optimizeSignature = guideSettings.treeoptimizesignature;
        guiding_fieldSubdivConfig.splitType = guideSettings.treesplittype;
        guiding_fieldSubdivConfig.confidenceType = guideSettings.treeconfidencetype;
        // TODO: support multiple models
            {
                auto &cfg = guiding_fieldSubdivConfig.signatureEnsembleConfig[0];
                cfg.basisType = guideSettings.treebasistype;
                switch (cfg.basisType) {
                    case PGL_BASIS_FUNC_NN:
                        cfg.setResolution(guideSettings.octahedralresolution);
                        break;
                    case PGL_BASIS_FUNC_SPLAT:
                        cfg.setResolution(guideSettings.octahedralresolution);
                        cfg.setSplatSigma(guideSettings.splatSigma);
                        break;
                    case PGL_BASIS_FUNC_DON_PCG:
                    case PGL_BASIS_FUNC_DON_XI:
                        cfg.setOctaveMin(guideSettings.octavemin);
                        cfg.setOctaveMax(guideSettings.octavemax);
                        cfg.setDONGamma(guideSettings.octaveGamma);
                        break;
                        break;
                    case PGL_BASIS_FUNC_LATITUDE:
                        cfg.setResolution(guideSettings.latituderesolution);
                        break;
                    case PGL_BASIS_FUNC_LONGITUDE:
                        cfg.setResolution(guideSettings.longituderesolution);
                        break;
                }

            }
        guiding_fieldSubdivConfig.defensiveType = guideSettings.treedefensivetype;
        guiding_fieldSubdivConfig.riskTolerance = guideSettings.treerisktolerance;
        guiding_fieldSubdivConfig.tValueThreshold = guideSettings.treetvaluethreshold;

        if (guideSettings.loadGuidingCache) {
            if(FileExists(guideSettings.guidingCacheFileName)) {
                guiding_field = new openpgl::cpp::Field(guiding_device, guideSettings.guidingCacheFileName);
                this->guideSettings.enableTraining = false;
            } else {
                std::cout << "Warning: Guiding cache file does not exists: guidingCacheFileName = " << guideSettings.guidingCacheFileName << std::endl;
                guiding_field = new openpgl::cpp::Field(guiding_device, guiding_fieldConfig);
            }
        } else {
            guiding_field = new openpgl::cpp::Field(guiding_device, guiding_fieldConfig);
        }
        guiding_field->UpdateSubdivConfig(guiding_fieldSubdivConfig);
        guiding_sampleStorage = new openpgl::cpp::SampleStorage();

        guiding_threadPathSegmentStorage = new ThreadLocal<openpgl::cpp::PathSegmentStorage*>(
        [this]() { openpgl::cpp::PathSegmentStorage* pss = new openpgl::cpp::PathSegmentStorage(true);
                   size_t maxPathSegments = std::max(settings.maxDepth*2, 30);
                   pss->Reserve(maxPathSegments);
                   pss->SetMaxDistance(guidingInfiniteLightDistance);
                   return pss;});

        guiding_threadSurfaceSamplingDistribution = new ThreadLocal<openpgl::cpp::SurfaceSamplingDistribution*>(
        [this]() { return new openpgl::cpp::SurfaceSamplingDistribution(guiding_field); });

        Vector2i resolution = camera.GetFilm().PixelBounds().Diagonal();
        sensor = camera.GetFilm().GetPixelSensor();

        if(guideSettings.loadContributionEstimate) {
            if(FileExists(guideSettings.contributionEstimateFileName)) {
                imageSpaceGuidingBuffer = new openpgl::cpp::util::ImageSpaceGuidingBuffer(guideSettings.contributionEstimateFileName);
                imageSpaceGuidingBufferReady = true;
                calculateImageSpaceGuidingBuffer = false;
            } else {
                std::cout << "Warning: Contribution estimate file does not exists: contributionEstimateFileName = " << guideSettings.contributionEstimateFileName << std::endl;
            }
        }

        if(!imageSpaceGuidingBufferReady && (guideSettings.storeContributionEstimate || guideSettings.guideRR)){
            calculateImageSpaceGuidingBuffer = true;
            imageSpaceGuidingBuffer = new openpgl::cpp::util::ImageSpaceGuidingBuffer(openpgl::cpp::Point2i(resolution[0], resolution[1]));
            imageSpaceGuidingBufferReady = false;
        }

        if(guideSettings.guideRR) {
            settings.minRRDepth = 1;
        }

        guidingCacheUpdateTime = 0;

      }

GuidedPathIntegrator::~GuidedPathIntegrator() {
    //~RayIntegrator();
    if(guideSettings.storeGuidingCache) {
        std::cout << "GuidedPathIntegrator storing guiding cache = " << guideSettings.guidingCacheFileName << std::endl;
        guiding_field->Store(guideSettings.guidingCacheFileName);
    }

    if(guideSettings.storeContributionEstimate){
        imageSpaceGuidingBuffer->Store(guideSettings.contributionEstimateFileName);
    }

    delete guiding_device;
    delete guiding_sampleStorage;
    delete guiding_field;
    delete imageSpaceGuidingBuffer;
}

void GuidedPathIntegrator::Render() {
    if (!Options->guidingViewer) return ImageTileIntegrator::Render();
    std::cout << "Running interactive guiding cache viewer mode." << std::endl;
    // Handle debugStart, if set
    if (!Options->debugStart.empty()) {
        std::vector<int> c = SplitStringToInts(Options->debugStart, ',');
        if (c.empty())
            ErrorExit("Didn't find integer values after --debugstart: %s",
                      Options->debugStart);
        if (c.size() != 3)
            ErrorExit("Didn't find three integer values after --debugstart: %s",
                      Options->debugStart);

        Point2i pPixel(c[0], c[1]);
        int sampleIndex = c[2];

        ScratchBuffer scratchBuffer(65536);
        Sampler tileSampler = samplerPrototype.Clone(Allocator());
        tileSampler.StartPixelSample(pPixel, sampleIndex);

        EvaluatePixelSample(pPixel, sampleIndex, tileSampler, scratchBuffer);
        return;
    }

    thread_local Point2i threadPixel;
    thread_local int threadSampleIndex;
    CheckCallbackScope _([&]() {
        return StringPrintf("Rendering failed at pixel (%d, %d) sample %d. Debug with "
                            "\"--debugstart %d,%d,%d\"\n",
                            threadPixel.x, threadPixel.y, threadSampleIndex,
                            threadPixel.x, threadPixel.y, threadSampleIndex);
    });

    // Declare common variables for rendering image in tiles
    ThreadLocal<ScratchBuffer> scratchBuffers([]() { return ScratchBuffer(); });

    ThreadLocal<Sampler> samplers([this]() { return samplerPrototype.Clone(); });

    Bounds2i pixelBounds = camera.GetFilm().PixelBounds();
    Timer totalRenderingTimer;

    if (Options->recordPixelStatistics)
        StatsEnablePixelStats(pixelBounds,
                              RemoveExtension(camera.GetFilm().GetFilename()));

    pstd::optional<Image> referenceImage;
    if (!Options->referenceImage.empty()) {
        auto mse = Image::Read(Options->referenceImage);
        referenceImage = mse.image;

        Bounds2i msePixelBounds =
            mse.metadata.pixelBounds
                ? *mse.metadata.pixelBounds
                : Bounds2i(Point2i(0, 0), referenceImage->Resolution());
        if (pixelBounds != msePixelBounds)
            ErrorExit("Pixel bounds of image %s and MSE reference image %s don't match.",
                pixelBounds, msePixelBounds);
    }

    if (!Options->displayServer.empty()) {
        Warning("Not supporting display server with --guidingviewer");
    }

    // Launch the GUI and render image in waves
    Application app(camera, aggregate, lights, std::move(referenceImage),
        guiding_device, guiding_field, *guiding_sampleStorage, guiding_fieldSubdivConfig,
        samplerPrototype, samplers, settings, guideSettings,
        [&](int waveStart) {
            // std::cout << "Rendering wave " << waveStart << std::endl;
            Timer pureRenderingTimer;
            // Render current wave's image tiles in parallel
            ParallelFor2D(pixelBounds, [&](Bounds2i tileBounds) {
                // Render image tile given by _tileBounds_
                ScratchBuffer &scratchBuffer = scratchBuffers.Get();
                Sampler &sampler = samplers.Get();
                PBRT_DBG("Starting image tile (%d,%d)-(%d,%d) waveStart %d, waveEnd %d\n",
                         tileBounds.pMin.x, tileBounds.pMin.y, tileBounds.pMax.x,
                         tileBounds.pMax.y, waveStart, waveEnd);
                for (Point2i pPixel : tileBounds) {
                    StatsReportPixelStart(pPixel);
                    threadPixel = pPixel;
                    // Render samples in pixel _pPixel_
                    threadSampleIndex = waveStart;
                    sampler.StartPixelSample(pPixel, waveStart);
                    EvaluatePixelSample(pPixel, waveStart, sampler, scratchBuffer);
                    scratchBuffer.Reset();

                    StatsReportPixelEnd(pPixel);
                }
                PBRT_DBG("Finished image tile (%d,%d)-(%d,%d)\n", tileBounds.pMin.x,
                         tileBounds.pMin.y, tileBounds.pMax.x, tileBounds.pMax.y);
            });
            pureRenderingTime += pureRenderingTimer.ElapsedSeconds();
        },
        [&](int waveEnd) {
            // std::cout << "Updating cache " << waveEnd << std::endl;
            PostProcessWave();  // Update guiding cache
        },
        [&](int waveEnd) {
            std::cout << "Writing image with spp = " << waveEnd << std::endl;
            ImageMetadata metadata;
            metadata.renderTimeSeconds = totalRenderingTimer.ElapsedSeconds();
            metadata.samplesPerPixel = waveEnd;
            camera.InitMetadata(&metadata);
            camera.GetFilm().WriteImage(metadata, 1.0f / waveEnd);
        },
        [&]() { return avgPathLength; });

    if (int ret = app.Run(); ret != 0)
        Error("Guiding viewer application failed with %d", ret);

    LOG_VERBOSE("Rendering finished");
}


void GuidedPathIntegrator::PostProcessWave() {
    avgPathLength = pathLengthCnt = 0;

    waveCounter++;
    std::cout << "GuidedPathIntegrator::PostProcessWave()" << std::endl;
    if (guideSettings.evaluateOnly) {
        std::cout << "Evaluation Pass" << std::endl;
        guiding_field->Evaluate(*guiding_sampleStorage);
    }
    else if (guideSettings.enableTraining) {
        const size_t numValidSamples = guiding_sampleStorage->GetSizeSurface() + guiding_sampleStorage->GetSizeVolume();
        std::cout << "Guiding Iteration: "<< guiding_field->GetIteration() << "\t numValidSamples: " << numValidSamples << std::endl;
        if(numValidSamples > 128) {
            Timer guidingFieldUpdateTimer;
            guiding_field->Update(*guiding_sampleStorage);
            guidingCacheUpdateTime += guidingFieldUpdateTimer.ElapsedSeconds();
            if(guiding_field->GetIteration() >= guideSettings.guideNumTrainingWaves) {
                guideSettings.enableTraining = false;
            }
        }
    }
    guiding_sampleStorage->Clear();

    if(calculateImageSpaceGuidingBuffer && waveCounter == std::pow(2.0f, imageSpaceGuidingBufferUpdateWave)) {
        Timer imageSpaceGuidingBufferTimer;
        imageSpaceGuidingBuffer->Update();
        imageSpaceGudingBufferUpdateTime += imageSpaceGuidingBufferTimer.ElapsedSeconds();
        imageSpaceGuidingBufferReady = true;
        imageSpaceGuidingBufferUpdateWave++;
    }
}

SampledSpectrum GuidedPathIntegrator::Li(Point2i pPixel, RayDifferential ray, SampledWavelengths &lambda,
                                   Sampler sampler, ScratchBuffer &scratchBuffer,
                                   VisibleSurface *visibleSurf) const {

    openpgl::cpp::PathSegmentStorage* pathSegmentStorage = guiding_threadPathSegmentStorage->Get();
    openpgl::cpp::SurfaceSamplingDistribution* surfaceSamplingDistribution = guiding_threadSurfaceSamplingDistribution->Get();

    openpgl::cpp::PathSegment* pathSegmentData = nullptr;

    openpgl::cpp::util::ImageSpaceGuidingBuffer::Sample cedSample;

    SampledSpectrum pixelContributionEstimate(0.f);
    SampledSpectrum adjointEstimate(0.f);
    bool guideRR = false;
    if (guideSettings.guideRR && imageSpaceGuidingBufferReady) {
        openpgl::cpp::Vector3f pgPixelContributionEstimate = imageSpaceGuidingBuffer->GetPixelContributionEstimate(openpgl::cpp::Point2i(pPixel[0], pPixel[1]));
        pixelContributionEstimate[0] = pgPixelContributionEstimate.x;
        pixelContributionEstimate[1] = pgPixelContributionEstimate.y;
        pixelContributionEstimate[2] = pgPixelContributionEstimate.z;
        guideRR = true;
    }

    // Declare local variables for GuidedPathIntegrator::Li()
    SampledSpectrum L(0.f), beta(1.f);
    SampledSpectrum bsdfWeight(1.f);
    int depth = 0;

    GuidedBSDF gbsdf(&sampler, guiding_field, surfaceSamplingDistribution, guideSettings.enableGuiding, guideSettings.surfaceGuidingType);
    float rr_correction = 1.0f;

    Float misPDF, etaScale = 1;
    bool specularBounce = false, anyNonSpecularBounces = false, wasRRorTT = true;
    LightSampleContext prevIntrCtx;

    bool add_direct_contribution = false;
    float w = 0.0f;
    // Sample path from camera and accumulate radiance estimate
    while (true) {
        Float survivalProb = 1.f;
        // Trace ray and find closest path vertex and its BSDF
        pstd::optional<ShapeIntersection> si = Intersect(ray);
        // Add emitted light at intersection point or from the environment
        if (!si) {
            // Incorporate emission from infinite lights for escaped ray
            for (const auto &light : infiniteLights) {
                SampledSpectrum Le = light.Le(ray, lambda);
                if (depth == 0 || specularBounce) {
                    L += beta * Le;
                    guiding_addInfiniteLightEmission(pathSegmentStorage, guidingInfiniteLightDistance, ray, Le, 1.0f, lambda, colorSpace);
                } else {
                    // Compute MIS weight for infinite light
                    Float lightPDF = lightSampler.PMF(prevIntrCtx, light) *
                                     light.PDF_Li(prevIntrCtx, ray.d, true);
                    Float w_b = settings.useNEE ? PowerHeuristic(1, misPDF, 1, lightPDF) : 1.0f;

                    L += beta * w_b * Le;
                    guiding_addInfiniteLightEmission(pathSegmentStorage, guidingInfiniteLightDistance, ray, Le, w_b, lambda, colorSpace);
                }
            }

            break;
        }
        // Incorporate emission from surface hit by ray
        SampledSpectrum Le = si->intr.Le(-ray.d, lambda);
        if (Le) {
            if (depth == 0 || specularBounce) {
                L += beta * Le;
                w = 1.0f;
                add_direct_contribution = true;
            } else {
                // Compute MIS weight for area light
                Light areaLight(si->intr.areaLight);
                Float lightPDF = lightSampler.PMF(prevIntrCtx, areaLight) *
                                 areaLight.PDF_Li(prevIntrCtx, ray.d, true);
                Float w_l = settings.useNEE ? PowerHeuristic(1, misPDF, 1, lightPDF) : 1.0f;
                L += beta * w_l * Le;
                                w = w_l;
                add_direct_contribution = true;
            }
        }

        SurfaceInteraction &isect = si->intr;
        // Get BSDF and skip over medium boundaries
        BSDF bsdf = isect.GetBSDF(ray, lambda, camera, scratchBuffer, sampler);
        if (!bsdf) {
            specularBounce = true;  // disable MIS if the indirect ray hits a light
            isect.SkipIntersection(&ray, si->tHit);
            continue;
        }

        pathSegmentData = guiding_newSurfacePathSegment(pathSegmentStorage, ray, si);

        if(add_direct_contribution)
        {
            guiding_addSurfaceEmission(pathSegmentData, Le, w, lambda, colorSpace);
        }
        add_direct_contribution = false;

        // Initialize _visibleSurf_ at first intersection
        if (depth == 0 && (visibleSurf || calculateImageSpaceGuidingBuffer)) {
            // Estimate BSDF's albedo
            // Define sample arrays _ucRho_ and _uRho_ for reflectance estimate
            constexpr int nRhoSamples = 16;
            const Float ucRho[nRhoSamples] = {
                0.75741637, 0.37870818, 0.7083487, 0.18935409, 0.9149363, 0.35417435,
                0.5990858,  0.09467703, 0.8578725, 0.45746812, 0.686759,  0.17708716,
                0.9674518,  0.2995429,  0.5083201, 0.047338516};
            const Point2f uRho[nRhoSamples] = {
                Point2f(0.855985, 0.570367), Point2f(0.381823, 0.851844),
                Point2f(0.285328, 0.764262), Point2f(0.733380, 0.114073),
                Point2f(0.542663, 0.344465), Point2f(0.127274, 0.414848),
                Point2f(0.964700, 0.947162), Point2f(0.594089, 0.643463),
                Point2f(0.095109, 0.170369), Point2f(0.825444, 0.263359),
                Point2f(0.429467, 0.454469), Point2f(0.244460, 0.816459),
                Point2f(0.756135, 0.731258), Point2f(0.516165, 0.152852),
                Point2f(0.180888, 0.214174), Point2f(0.898579, 0.503897)};

            const SampledSpectrum albedo = bsdf.rho(isect.wo, ucRho, uRho);
            if(visibleSurf)
                *visibleSurf = VisibleSurface(isect, albedo, lambda);

            const RGB albedoRGB = albedo.ToRGB(lambda, *colorSpace);
            cedSample.albedo = openpgl::cpp::Vector3f(albedoRGB[0], albedoRGB[1], albedoRGB[2]);
            cedSample.normal = openpgl::cpp::Vector3f(isect.n[0], isect.n[1], isect.n[2]);
            cedSample.SetSurfaceEvent(true);
        }

        // End path if maximum depth reached
        if (depth++ == settings.maxDepth)
            break;

        // Possibly regularize the BSDF
        if (settings.regularize && anyNonSpecularBounces) {
            ++regularizedBSDFs;
            bsdf.Regularize();
        }

        ++totalBSDFs;

        // Guiding - Check if we can use guiding. If so intialize the guiding distribution
        Float v = guideSettings.knnLookup ? sampler.Get1D(): -1.0f;
        bool cacheInitialized = gbsdf.init(&bsdf, ray, si, v);
        adjointEstimate = gbsdf.OutgoingRadiance(-ray.d);

        if (guideRR && depth > settings.minRRDepth) {
            survivalProb = specularBounce ? 0.95 : openpgl::cpp::util::GuidedRussianRoulette(OPGLVector3f(beta), OPGLVector3f(adjointEstimate), OPGLVector3f(pixelContributionEstimate), 0.1f);
        }

        // Initialize _visibleSurf_ at first nonspecular intersection
        // To avoid the ambiguity from specular transmissive surfaces (reflective or transmissive), we only store if it was a transmission
        bool shouldCreateVisbleSurf = visibleSurf && !anyNonSpecularBounces && wasRRorTT && IsNonSpecular(bsdf.Flags());

        if (cacheInitialized && shouldCreateVisbleSurf) {
            Point3 p = si->intr.p();
            pgl_point3f pglP{p.x, p.y, p.z};
            auto [coarse, fine] = guiding_field->GetCoarseFineRegionStatisticsSurface(pglP);
            visibleSurf->guidingData.id = coarse.id;
            visibleSurf->guidingData.fineId = fine.id;
            if (coarse.id != -1) {
                visibleSurf->guidingData.numSamples = coarse.numSamples;
                visibleSurf->guidingData.numZeroValueSamples = coarse.numZeroValueSamples;
                visibleSurf->guidingData.depth = coarse.depth;
                if (fine.id != -1) {
                    visibleSurf->guidingData.fluence = fine.fluence;
                    visibleSurf->guidingData.energy = fine.energy;  // max energy along the path
                    visibleSurf->guidingData.risk = fine.risk;  // max risk along the path
                    visibleSurf->guidingData.tValue = fine.tValue;
                } else {
                    visibleSurf->guidingData.fluence = coarse.fluence;
                    visibleSurf->guidingData.energy = coarse.energy;
                    visibleSurf->guidingData.risk = coarse.risk;
                    visibleSurf->guidingData.tValue = coarse.tValue;
                }
            }
        }

        // Sample direct illumination from the light sources
        if (settings.useNEE && IsNonSpecular(bsdf.Flags())) {
            ++totalPaths;
            SampledSpectrum Ld = SampleLd(isect, &gbsdf, survivalProb, lambda, sampler);
            if (!Ld)
                ++zeroRadiancePaths;
            L += beta * Ld;
            // Guiding - add scattered contribution from NEE
            guiding_addScatteredDirectLight(pathSegmentData, Ld, lambda, colorSpace);
        }

        // Sample BSDF to get new path direction
        Vector3f wo = -ray.d;
        Float u = sampler.Get1D();
        pstd::optional<BSDFSample> bs = gbsdf.Sample_f(wo, u, sampler.Get2D());
        if (!bs)
            break;

        rr_correction *= bs->pdf / bs->bsdfPdf;
        misPDF = survivalProb * bs->misPdf;
        // Update path state variables after surface scattering
        bsdfWeight = bs->f * AbsDot(bs->wi, isect.shading.n) / bs->pdf;
        beta *= bsdfWeight;

        DCHECK(!IsInf(beta.y(lambda)));
        specularBounce = bs->IsSpecular();
        anyNonSpecularBounces |= !bs->IsSpecular();
        if (bs->IsTransmission())
            etaScale *= Sqr(bs->eta);
        wasRRorTT &= !IsTransmissive(bsdf.Flags()) || bs->IsTransmission();
        prevIntrCtx = si->intr;

        ray = isect.SpawnRay(ray, bsdf, bs->wi, bs->flags, bs->eta);

        if (!beta)
            break;
        // Possibly terminate the path with Russian roulette
        //SampledSpectrum rrBeta = beta * etaScale;
        // termination probability
        // Todo: need to find a better solution for specular and near specular surfaces

        if (!guideRR && depth > settings.minRRDepth) {
            const SampledSpectrum rrThroughputWeight = beta * rr_correction * etaScale;
            survivalProb = specularBounce ? 0.95 : openpgl::cpp::util::StandardThroughputBasedRussianRoulette(OPGLVector3f(rrThroughputWeight));
        }
        if (survivalProb < 1 && depth > settings.minRRDepth) {
            Float q = std::max<Float>(0, 1 - survivalProb);
            if (sampler.Get1D() < q)
                break;
            beta /= 1 - q;
            DCHECK(!IsInf(beta.y(lambda)));
        }

        // Guiding - Add BSDF data to the current path segment
        guiding_addSurfaceData(pathSegmentData, bsdfWeight, bs->wi, bs->eta, bs->sampledRoughness, bs->pdf,
#ifdef OPENPGL_GUIDING_PDF_CACHES
                               bs->guidingPDF,
#endif
                               survivalProb, lambda, colorSpace);
    }
    pathLength << depth;
    {
        std::lock_guard lock(pathLengthMutex);
        pathLengthCnt += 1;
        avgPathLength = Lerp(1.f / pathLengthCnt, avgPathLength, (float) depth);
    }

    if(calculateImageSpaceGuidingBuffer)
    {
    #if defined(PBRT_RGB_RENDERING)
        RGB color = L.ToRGB(lambda, *colorSpace);
    #else
        RGB color = sensor->ToSensorRGB(L, lambda);
    #endif
        cedSample.contribution = openpgl::cpp::Vector3f(color[0], color[1], color[2]);
        imageSpaceGuidingBuffer->AddSample(openpgl::cpp::Point2i(pPixel[0], pPixel[1]), cedSample);
    }

    if (guideSettings.enableTraining || guideSettings.evaluateOnly)
    {
        //pathSegmentStorage->ValidateSegments();
        pathSegmentStorage->PropagateSamples(guiding_sampleStorage, true, true);
        pathSegmentStorage->Clear();
    }
    else
    {
        pathSegmentStorage->Clear();
    }
    return L;
}

SampledSpectrum GuidedPathIntegrator::SampleLd(const SurfaceInteraction &intr, const GuidedBSDF *bsdf, const Float survivalProb,
                                         SampledWavelengths &lambda,
                                         Sampler sampler) const {
    // Initialize _LightSampleContext_ for light sampling
    LightSampleContext ctx(intr);
    // Try to nudge the light sampling position to correct side of the surface
    BxDFFlags flags = bsdf->Flags();
    if (IsReflective(flags) && !IsTransmissive(flags))
        ctx.pi = intr.OffsetRayOrigin(intr.wo);
    else if (IsTransmissive(flags) && !IsReflective(flags))
        ctx.pi = intr.OffsetRayOrigin(-intr.wo);

    // Choose a light source for the direct lighting calculation
    Float u = sampler.Get1D();
    pstd::optional<SampledLight> sampledLight = lightSampler.Sample(ctx, u);
    Point2f uLight = sampler.Get2D();
    if (!sampledLight)
        return {};

    // Sample a point on the light source for direct lighting
    Light light = sampledLight->light;
    DCHECK(light && sampledLight->p > 0);
    pstd::optional<LightLiSample> ls = light.SampleLi(ctx, uLight, lambda, true);
    if (!ls || !ls->L || ls->pdf == 0)
        return {};

    // Evaluate BSDF for light sample and check light visibility
    Vector3f wo = intr.wo, wi = ls->wi;
    SampledSpectrum f = bsdf->f(wo, wi) * AbsDot(wi, intr.shading.n);
    if (!f || !Unoccluded(intr, ls->pLight))
        return {};

    // Return light's contribution to reflected radiance
    Float p_l = sampledLight->p * ls->pdf;
    if (IsDeltaLight(light.Type()))
        return ls->L * f / p_l;
    else {
        Float p_b = survivalProb * bsdf->PDF(wo, wi);
        Float w_l = PowerHeuristic(1, p_l, 1, p_b);
        return w_l * ls->L * f / p_l;
    }
}

std::string GuidedPathIntegrator::ToString() const {
    return StringPrintf("[ GuidedPathIntegrator maxDepth: %d lightSampler: %s regularize: %s ]",
                        settings.maxDepth, lightSampler, settings.regularize);
}

void GuidedPathIntegrator::LogFileHead(FILE *logFile) const {
    fprintf(logFile, "training time, number of regions, ");
}

void GuidedPathIntegrator::LogFileRow(FILE *logFile) const {
    fprintf(logFile, "%.9f, %ld, ", guidingCacheUpdateTime, guiding_field->GetRegionCountSurface(false));
}

std::unique_ptr<GuidedPathIntegrator> GuidedPathIntegrator::Create(
    const ParameterDictionary &parameters, const RGBColorSpace *colorSpace, Camera camera, Sampler sampler,
    Primitive aggregate, std::vector<Light> lights, const FileLoc *loc) {
    int maxDepth = parameters.GetOneInt("maxdepth", 5);
    int minRRDepth = parameters.GetOneInt("minrrdepth", 1);
    bool useNEE = parameters.GetOneBool("usenee", true);
    GuidingSettings settings;
    settings.enableGuiding = parameters.GetOneBool("enableguiding", true);

    settings.guideSurface = parameters.GetOneBool("surfaceguiding", true);
    settings.guideRR = parameters.GetOneBool("rrguiding", false);
    settings.deterministic = parameters.GetOneBool("deterministic", true);

    settings.knnLookup = parameters.GetOneBool("knnlookup", true);
    std::string strSurfaceGuidingType = parameters.GetOneString("surfaceguidingtype", "ris");
    settings.surfaceGuidingType = strSurfaceGuidingType == "mis" ? EGuideMIS : EGuideRIS;

    settings.guideNumTrainingWaves = parameters.GetOneInt("numtrainingwaves", 128);
    auto dtype = parameters.GetOneString("dtype", "pavmm");
    if (dtype == "pavmm") settings.dtype = PGL_DIRECTIONAL_DISTRIBUTION_PARALLAX_AWARE_VMM;
    else if (dtype == "vmm") settings.dtype = PGL_DIRECTIONAL_DISTRIBUTION_VMM;
    else if (dtype == "quadtree") settings.dtype = PGL_DIRECTIONAL_DISTRIBUTION_QUADTREE;
    else throw std::runtime_error("Unknown dtype: " + dtype);
    settings.treesamplecountthreshold = parameters.GetOneInt("treesamplecountthreshold", settings.treesamplecountthreshold);
    settings.treeforcedsamplecountthreshold = parameters.GetOneInt("treeforcedsamplecountthreshold", settings.treeforcedsamplecountthreshold);
    settings.treeminsamplescandidatesplit = parameters.GetOneInt("treeminsamplescandidatesplit", settings.treeminsamplescandidatesplit);
    settings.treeminsamplespromotion = parameters.GetOneInt("treeminsamplespromotion", settings.treeminsamplespromotion);
    settings.treemaxdepth = parameters.GetOneInt("treemaxdepth", settings.treemaxdepth);
    settings.treeinitializingiters = parameters.GetOneInt("treeinitializingiters", settings.treeinitializingiters);
    settings.treelookaheaddepth = parameters.GetOneInt("treelookaheaddepth", settings.treelookaheaddepth);
    settings.treeadaptivethreshold = parameters.GetOneFloat("treeadaptivethreshold", settings.treeadaptivethreshold);
    settings.treestdmultiplier = parameters.GetOneFloat("treestdmultiplier", settings.treestdmultiplier);
    settings.treerisktolerance = parameters.GetOneFloat("treerisktolerance", settings.treerisktolerance);
    settings.treetvaluethreshold = parameters.GetOneFloat("treetvaluethreshold", settings.treetvaluethreshold);
    settings.treeinlierpercent = parameters.GetOneFloat("treeinlierpercent", settings.treeinlierpercent);
    settings.treedborstdmultiplier = parameters.GetOneFloat("treedborstdmultiplier", settings.treedborstdmultiplier);
    settings.treetepsk = parameters.GetOneFloat("treetepsk", settings.treetepsk);
    settings.treecedecay = parameters.GetOneFloat("treecedecay", settings.treecedecay);
    settings.treeenablepromotion = parameters.GetOneBool("treeenablepromotion", settings.treeenablepromotion);
    settings.treemultiplycosine = parameters.GetOneBool("treemultiplycosine", settings.treemultiplycosine);
    settings.treereproject = parameters.GetOneBool("treereproject", settings.treereproject);
    settings.treenonrecursive = parameters.GetOneBool("treenonrecursive", settings.treenonrecursive);
    settings.treesinglepromotion = parameters.GetOneBool("treesinglepromotion", settings.treesinglepromotion);
    settings.treeoptimizesignature = parameters.GetOneBool("treeoptimizesignature", settings.treeoptimizesignature);
    auto splittype = parameters.GetOneString("treesplittype", "baseline");
    if (splittype == "baseline") settings.treesplittype = PGL_SPATIAL_SPLIT_BASELINE;
    else if (splittype == "vs") settings.treesplittype = PGL_SPATIAL_SPLIT_VS;
    else if (splittype == "igs") settings.treesplittype = PGL_SPATIAL_SPLIT_IGS;
    else if (splittype == "fs") settings.treesplittype = PGL_SPATIAL_SPLIT_FS;
    else throw std::runtime_error("Unknown treesplittype: " + splittype);

    auto confidencetype = parameters.GetOneString("treeconfidencetype", "none");
    if (confidencetype == "none") settings.treeconfidencetype = PGL_SPATIAL_CONFIDENCE_NONE;
    else if (confidencetype == "risk") settings.treeconfidencetype = PGL_SPATIAL_CONFIDENCE_RISK;
    else if (confidencetype == "welch") settings.treeconfidencetype = PGL_SPATIAL_CONFIDENCE_TTEST;
    else if (confidencetype == "ttest_per_bin") settings.treeconfidencetype = PGL_SPATIAL_CONFIDENCE_TTEST_PER_BIN;
    else throw std::runtime_error("Unknown treeconfidencetype: " + confidencetype);

    auto basistype = parameters.GetOneString("treebasistype", "nn");
    if (basistype == "nn") settings.treebasistype = PGL_BASIS_FUNC_NN;
    else if (basistype == "splat") settings.treebasistype = PGL_BASIS_FUNC_SPLAT;
    else if (basistype == "don_pcg") settings.treebasistype = PGL_BASIS_FUNC_DON_PCG;
    else if (basistype == "don_xi") settings.treebasistype = PGL_BASIS_FUNC_DON_XI;
    else throw std::runtime_error("Unknown treecontribtype: " + basistype);

    auto defensivetype = parameters.GetOneString("treedefensivetype", "fixed");
    if (defensivetype == "fixed") settings.treedefensivetype = PGL_SPATIAL_DEFENSIVE_FIXED;
    else if (defensivetype == "sqrt") settings.treedefensivetype = PGL_SPATIAL_DEFENSIVE_SQRT;
    else if (defensivetype == "ppg") settings.treedefensivetype = PGL_SPATIAL_DEFENSIVE_PPG;
    else throw std::runtime_error("Unknown treedefensivetype: " + defensivetype);

    auto filtertype = parameters.GetOneString("treefiltertype", "none");
    if (filtertype == "none") settings.treefiltertype = PGL_SPATIAL_FILTER_NONE;
    else if (filtertype == "percentage") settings.treefiltertype = PGL_SPATIAL_FILTER_PERCENTAGE;
    else if (filtertype == "dbor") settings.treefiltertype = PGL_SPATIAL_FILTER_DBOR;
    else if (filtertype == "dbor_accum") settings.treefiltertype = PGL_SPATIAL_FILTER_DBOR_ACCUM;
    else throw std::runtime_error("Unknown treefiltertype: " + filtertype);

    settings.numbins = parameters.GetOneInt("numbins", settings.numbins);
    if (settings.numbins <= 0 || settings.numbins > 8)
        ErrorExit(loc, "Invalid number of bins %d: only 1-8 are supported.", settings.numbins);
    settings.octahedralresolution = parameters.GetOneInt("octahedralresolution", settings.octahedralresolution);
    if (settings.octahedralresolution < 0)
        ErrorExit(loc, "Invalid octahedral resolution %d: only positive values are supported.", settings.octahedralresolution);
    settings.splatSigma = parameters.GetOneFloat("splatsigma", settings.splatSigma);
    settings.octavemin = parameters.GetOneInt("octavemin", settings.octavemin);
    settings.octavemax = parameters.GetOneInt("octavemax", settings.octavemax);
    if (settings.octavemin <= 0 || settings.octavemax <= 0 || settings.octavemin > settings.octavemax)
        ErrorExit(loc, "Invalid octave range [%d, %d].", settings.octavemin, settings.octavemax);
    settings.octaveGamma = parameters.GetOneFloat("octavegamma", settings.octaveGamma);
    settings.latituderesolution = parameters.GetOneInt("latituderesolution", settings.latituderesolution);
    if (settings.latituderesolution <= 0)
        ErrorExit(loc, "Invalid latitude resolution %d: only positive values are supported.", settings.latituderesolution);
    settings.longituderesolution = parameters.GetOneInt("longituderesolution", settings.longituderesolution);
    if (settings.longituderesolution <= 0)
        ErrorExit(loc, "Invalid longitude resolution %d: only positive values are supported.", settings.longituderesolution);

    settings.storeGuidingCache = parameters.GetOneBool("storeGuidingCache", false);
    settings.loadGuidingCache = parameters.GetOneBool("loadGuidingCache", false);
    settings.guidingCacheFileName = parameters.GetOneString("guidingCacheFileName", "");

    settings.storeContributionEstimate = parameters.GetOneBool("storeContributionEstimate", false);
    settings.loadContributionEstimate = parameters.GetOneBool("loadContributionEstimate", false);
    settings.contributionEstimateFileName = parameters.GetOneString("contributionEstimateFileName", "");

    std::string lightStrategy = parameters.GetOneString("lightsampler", "bvh");
    bool regularize = parameters.GetOneBool("regularize", false);

    return std::make_unique<GuidedPathIntegrator>(maxDepth, minRRDepth, useNEE, settings, colorSpace, camera, sampler, aggregate, lights,
                                            lightStrategy, regularize);
}

// GuidedVolPathIntegrator Method Definitions
GuidedVolPathIntegrator::GuidedVolPathIntegrator(int maxDepth, int minRRDepth, bool useNEE, const GuidingSettings guideSettings, const RGBColorSpace *colorSpace, Camera camera, Sampler sampler, Primitive aggregate,
                      std::vector<Light> lights,
                      const std::string &lightSampleStrategy,
                      bool regularize)
    : RayIntegrator(camera, sampler, aggregate, lights),
        maxDepth(maxDepth),
        minRRDepth(minRRDepth),
        useNEE(useNEE),
        guideSettings(guideSettings),
        colorSpace(colorSpace),
        lightSampler(LightSampler::Create(lightSampleStrategy, lights, Allocator())),
        regularize(regularize) {
        std::cout<< "GuidedVolPathIntegrator:" <<std::endl;
        std::cout<< "\t maxDepth = " << maxDepth << std::endl;
        std::cout<< "\t minRRDepth = " << minRRDepth << std::endl;
        std::cout<< "\t useNEE = " << useNEE << std::endl;
        std::cout<< "\t surfaceGuiding = " << guideSettings.guideSurface << std::endl;
        std::cout<< "\t volumeGuiding = " << guideSettings.guideVolume << std::endl;
        std::cout<< "\t surfaceGuidingType = " << guideSettings.surfaceGuidingType << std::endl;
        std::cout<< "\t volumeGuidingType = " << guideSettings.volumeGuidingType << std::endl;
        std::cout<< "\t loadGuidingCache = " << guideSettings.loadGuidingCache << std::endl;
        std::cout<< "\t guidingCacheFileName = " << guideSettings.guidingCacheFileName << std::endl;
        std::cout<< "\t lightSampleStrategy = " << lightSampleStrategy << std::endl;
        std::cout<< "\t regularize = " << regularize << std::endl;

        guiding_device = new openpgl::cpp::Device(PGL_DEVICE_TYPE_CPU_4);
        guiding_fieldConfig.Init(PGL_SPATIAL_STRUCTURE_KDTREE, PGL_DIRECTIONAL_DISTRIBUTION_PARALLAX_AWARE_VMM);

        if (guideSettings.loadGuidingCache) {
            if(FileExists(guideSettings.guidingCacheFileName)) {
                std::cout<< "GuidedVolPathIntegrator: loading guiding cache = "<< guideSettings.guidingCacheFileName <<std::endl;
                guiding_field = new openpgl::cpp::Field(guiding_device, guideSettings.guidingCacheFileName);
                guideTraining = false;
            } else {
                guiding_field = new openpgl::cpp::Field(guiding_device, guiding_fieldConfig);
            }
        } else {
            guiding_field = new openpgl::cpp::Field(guiding_device, guiding_fieldConfig);
        }
        guiding_sampleStorage = new openpgl::cpp::SampleStorage();

        guiding_threadPathSegmentStorage = new ThreadLocal<openpgl::cpp::PathSegmentStorage*>(
        [this]() { openpgl::cpp::PathSegmentStorage* pss = new openpgl::cpp::PathSegmentStorage(true);
                size_t maxPathSegments = this->maxDepth >= 1 ? this->maxDepth*2 : 30;
                pss->Reserve(maxPathSegments);
                pss->SetMaxDistance(guidingInfiniteLightDistance);
                return pss;});

        guiding_threadSurfaceSamplingDistribution = new ThreadLocal<openpgl::cpp::SurfaceSamplingDistribution*>(
        [this]() { return new openpgl::cpp::SurfaceSamplingDistribution(guiding_field); });

        guiding_threadVolumeSamplingDistribution = new ThreadLocal<openpgl::cpp::VolumeSamplingDistribution*>(
        [this]() { return new openpgl::cpp::VolumeSamplingDistribution(guiding_field); });

        Vector2i resolution = camera.GetFilm().PixelBounds().Diagonal();
        sensor = camera.GetFilm().GetPixelSensor();

        if(guideSettings.loadContributionEstimate) {
            if(FileExists(guideSettings.contributionEstimateFileName)) {
                imageSpaceGuidingBuffer = new openpgl::cpp::util::ImageSpaceGuidingBuffer(guideSettings.contributionEstimateFileName);
                imageSpaceGuidingBufferReady = true;
                calculateImageSpaceGuidingBuffer = false;
            } else {
                std::cout << "Warning: Contribution estimate file does not exists: contributionEstimateFileName = " << guideSettings.contributionEstimateFileName << std::endl;
            }
        }

        if(!imageSpaceGuidingBufferReady && (guideSettings.storeContributionEstimate || guideSettings.guideRR)){
            calculateImageSpaceGuidingBuffer = true;
            imageSpaceGuidingBuffer = new openpgl::cpp::util::ImageSpaceGuidingBuffer(openpgl::cpp::Point2i(resolution[0],resolution[1]));
            imageSpaceGuidingBufferReady = false;
        }

        if(guideSettings.guideRR) {
            this->minRRDepth = 1;
        }
}


GuidedVolPathIntegrator::~GuidedVolPathIntegrator() {
    //~RayIntegrator();
    if(guideSettings.storeGuidingCache) {
        std::cout << "GuidedVolPathIntegrator storing guiding cache = " << guideSettings.guidingCacheFileName << std::endl;
        guiding_field->Store(guideSettings.guidingCacheFileName);
    }

    if(guideSettings.storeContributionEstimate){
        imageSpaceGuidingBuffer->Store(guideSettings.contributionEstimateFileName);
    }

    delete guiding_device;
    delete guiding_sampleStorage;
    delete guiding_field;
    delete imageSpaceGuidingBuffer;
}

void GuidedVolPathIntegrator::PostProcessWave() {

    waveCounter++;
    std::cout << "GuidedVolPathIntegrator::PostProcessWave()" << std::endl;
    if(guideTraining) {
        const size_t numValidSamples = guiding_sampleStorage->GetSizeSurface() + guiding_sampleStorage->GetSizeVolume();
        std::cout << "Guiding Iteration: "<< guiding_field->GetIteration() << "\t numValidSamples: " << numValidSamples << "\t surfaceSamples: " << guiding_sampleStorage->GetSizeSurface() << "\t volumeSamples: " << guiding_sampleStorage->GetSizeVolume() << std::endl;
        if(numValidSamples > 128) {
            Timer guidingFiledUpdateTimer;
            guiding_field->Update(*guiding_sampleStorage);
            guidingCacheUpdateTime += guidingFiledUpdateTimer.ElapsedSeconds();
            if(guiding_field->GetIteration() >= guideSettings.guideNumTrainingWaves) {
                guideTraining = false;
            }
            guiding_sampleStorage->Clear();
        }
    }

    guiding_sampleStorage->Clear();

    if(calculateImageSpaceGuidingBuffer && waveCounter == std::pow(2.0f, imageSpaceGuidingBufferUpdateWave)) {
        Timer imageSpaceGuidingBufferTimer;
        imageSpaceGuidingBuffer->Update();
        imageSpaceGudingBufferUpdateTime += imageSpaceGuidingBufferTimer.ElapsedSeconds();
        std::cout << "Denoiser::time = " << imageSpaceGuidingBufferTimer.ElapsedSeconds() << std::endl;
        imageSpaceGuidingBufferReady = true;
        imageSpaceGuidingBufferUpdateWave++;
    }
}

SampledSpectrum GuidedVolPathIntegrator::Li(Point2i pPixel, RayDifferential ray, SampledWavelengths &lambda,
                                      Sampler sampler, ScratchBuffer &scratchBuffer,
                                      VisibleSurface *visibleSurf) const {

    openpgl::cpp::PathSegmentStorage* pathSegmentStorage = guiding_threadPathSegmentStorage->Get();
    openpgl::cpp::SurfaceSamplingDistribution* surfaceSamplingDistribution = guiding_threadSurfaceSamplingDistribution->Get();
    openpgl::cpp::VolumeSamplingDistribution* volumeSamplingDistribution = guiding_threadVolumeSamplingDistribution->Get();

    openpgl::cpp::PathSegment* pathSegmentData = nullptr;

    openpgl::cpp::util::ImageSpaceGuidingBuffer::Sample cedSample;

    SampledSpectrum pixelContributionEstimate(1.f);
    SampledSpectrum adjointEstimate(1.f);
    bool guideRR = false;
    const bool guideSurfaceRR = guideSettings.guideSurfaceRR;
    const bool guideVolumeRR = guideSettings.guideVolumeRR;
    if (guideSettings.guideRR && imageSpaceGuidingBufferReady) {
        openpgl::cpp::Vector3f pgPixelContributionEstimate = imageSpaceGuidingBuffer->GetPixelContributionEstimate(openpgl::cpp::Point2i(pPixel[0], pPixel[1]));
        pixelContributionEstimate[0] = pgPixelContributionEstimate.x;
        pixelContributionEstimate[1] = pgPixelContributionEstimate.y;
        pixelContributionEstimate[2] = pgPixelContributionEstimate.z;
        guideRR = true;
    }

    // Declare state variables for volumetric path sampling
    SampledSpectrum L(0.f), beta(1.f), r_u(1.f), r_l(1.f);
    bool specularBounce = false, anyNonSpecularBounces = false;
    int depth = 0;
    Float etaScale = 1;

    GuidedBSDF gbsdf(&sampler, guiding_field, surfaceSamplingDistribution, guideSettings.guideSurface, guideSettings.surfaceGuidingType);
    GuidedPhaseFunction gphase(&sampler, guiding_field, volumeSamplingDistribution, guideSettings.guideVolume, guideSettings.volumeGuidingType);
    float rr_correction = 1.0f;
    float misPDF = 1.0f;

    SampledSpectrum bsdfWeight(1.f);
    bool add_direct_contribution = false;
    Float w = 0.f;

    LightSampleContext prevIntrContext;

    while (true) {
        Float survivalProb = 1.f;
        // Sample segment of volumetric scattering path
        PBRT_DBG("%s\n", StringPrintf("Path tracer depth %d, current L = %s, beta = %s\n",
                                      depth, L, beta)
                             .c_str());
        pstd::optional<ShapeIntersection> si = Intersect(ray);

        SampledSpectrum transmittanceWeight = SampledSpectrum(1.0f);
        if (ray.medium) {
            // Sample the participating medium
            bool scattered = false, terminated = false;
            Float tMax = si ? si->tHit : Infinity;
            // Initialize _RNG_ for sampling the majorant transmittance
            uint64_t hash0 = Hash(sampler.Get1D());
            uint64_t hash1 = Hash(sampler.Get1D());
            RNG rng(hash0, hash1);

            SampledSpectrum T_maj = SampleT_maj(
                ray, tMax, sampler.Get1D(), rng, lambda,
                [&](Point3f p, MediumProperties mp, SampledSpectrum sigma_maj,
                    SampledSpectrum T_maj) {
                    // Handle medium scattering event for ray
                    if (!beta) {
                        terminated = true;
                        return false;
                    }
                    ++volumeInteractions;
                    // Add emission from medium scattering event
                    if (depth < maxDepth && mp.Le) {
                        // Compute $\beta'$ at new path vertex
                        Float pdf = sigma_maj[lambda.ChannelIdx()] * T_maj[lambda.ChannelIdx()];
                        SampledSpectrum betap = beta * T_maj / pdf;

                        // Compute rescaled path probability for absorption at path vertex
                        SampledSpectrum r_e = r_u * sigma_maj * T_maj / pdf;

                        // Update _L_ for medium emission
                        if (r_e)
                            L += betap * mp.sigma_a * mp.Le / r_e.Average();
                    }

                    // Compute medium event probabilities for interaction
#if defined(VOLUME_ABSORB)
                    Float pAbsorb = mp.sigma_a[lambda.ChannelIdx()] / sigma_maj[lambda.ChannelIdx()];
                    Float pScatter = mp.sigma_s[lambda.ChannelIdx()] / sigma_maj[lambda.ChannelIdx()];
                    Float pNull = std::max<Float>(0, 1 - pAbsorb - pScatter);

                    CHECK_GE(1 - pAbsorb - pScatter, -1e-6);
                    // Sample medium scattering event type and update path
                    Float um = rng.Uniform<Float>();
                    int mode = SampleDiscrete({pAbsorb, pScatter, pNull}, um);
                    if (mode == 0) {
                        // Handle absorption along ray path
                        terminated = true;
                        return false;

                    } else if (mode == 1) {
#else
                    SampledSpectrum sigma_t = mp.sigma_s + mp.sigma_a;
                    SampledSpectrum albedo = mp.sigma_s / sigma_t;
                    Float pScatter = sigma_t[lambda.ChannelIdx()] / sigma_maj[lambda.ChannelIdx()];
                    Float pNull = std::max<Float>(0, 1 - pScatter);

                    CHECK_GE(1 - pScatter, -1e-6);
                    // Sample medium scattering event type and update path
                    Float um = rng.Uniform<Float>();
                    int mode = SampleDiscrete({pScatter, pNull}, um);
                    if (mode == 0) {
#endif

                        if(depth==0) {
                            SampledSpectrum albedo = mp.sigma_s / (mp.sigma_s + mp.sigma_a);
                            RGB albedoRGB = albedo.ToRGB(lambda, *colorSpace);

                            cedSample.albedo = openpgl::cpp::Vector3f(albedoRGB[0], albedoRGB[1], albedoRGB[2]);
                            cedSample.normal = openpgl::cpp::Vector3f(-ray.d[0], -ray.d[1], -ray.d[2]);
                            cedSample.SetSurfaceEvent(false);
                        }

                        // Handle scattering along ray path
                        // Stop path sampling if maximum depth has been reached
                        if (depth++ >= maxDepth) {
                            terminated = true;
                            return false;
                        }

                        // Update _beta_ and _r_u_ for real-scattering event
#if defined(VOLUME_ABSORB)
                        Float pdf = T_maj[lambda.ChannelIdx()] * mp.sigma_s[lambda.ChannelIdx()];
                        beta *= T_maj * mp.sigma_s / pdf;
                        r_u *= T_maj * mp.sigma_s / pdf;
#else
                        Float pdf = T_maj[lambda.ChannelIdx()] * sigma_t[lambda.ChannelIdx()];
                        beta *= T_maj * mp.sigma_s / pdf;
                        r_u *= T_maj * sigma_t / pdf;
#endif
                        transmittanceWeight *= (T_maj * mp.sigma_s) / pdf;
                        guiding_addTransmittanceWeight(pathSegmentData, transmittanceWeight, lambda, colorSpace);
                        pathSegmentData = guiding_newVolumePathSegment(pathSegmentStorage, p, -ray.d);
                        transmittanceWeight = SampledSpectrum(1.0f);

                        if (beta && r_u) {
                            // Sample direct lighting at volume-scattering event
                            MediumInteraction intr(p, -ray.d, ray.time, ray.medium,
                                                   mp.phase);

                            Float v = sampler.Get1D();
                            gphase.init(&intr.phase, p, ray.d, v);
                            if(guideRR && guideVolumeRR) {
                                adjointEstimate = gphase.InscatteredRadiance(-ray.d, true);
                            }

                            // calculate survival property
                            survivalProb = 1.0f;
                            if (depth > minRRDepth) {
                                if (guideRR) {
                                    if(guideVolumeRR){
                                        survivalProb = specularBounce ? 0.95 : openpgl::cpp::util::GuidedRussianRoulette(OPGLVector3f(beta), OPGLVector3f(adjointEstimate), OPGLVector3f(pixelContributionEstimate), 0.1f);
                                    } else {
                                        survivalProb = 1.f;
                                    }
                                } else {
                                    const SampledSpectrum rrThroughputWeight = (beta / r_u.Average()) * rr_correction;
                                    survivalProb = specularBounce ? 0.95 : openpgl::cpp::util::StandardThroughputBasedRussianRoulette(OPGLVector3f(rrThroughputWeight));
                                }
                            }

                            // Preform next-event estimation before RR
                            if(useNEE){
                                SampledSpectrum Ld = SampleLd(intr, nullptr, &gphase, 1.0f, lambda, sampler, r_u);
                                L += beta * Ld;

                                // Guiding - add scattered contribution from NEE
                                guiding_addScatteredDirectLight(pathSegmentData, Ld, lambda, colorSpace);
                            }

                            // Perform stochastic path termination (Russian Roulette)
                            if (survivalProb < 1 && depth > minRRDepth) {
                                Float q = std::max<Float>(0, 1 - survivalProb);
                                if (sampler.Get1D() < q){
                                    terminated = true;
                                    return false;
                                }
                                beta /= 1 - q;
                            }

                            // Continue path
                            // Sample new direction at real-scattering event
                            Point2f u = sampler.Get2D();
                            pstd::optional<PhaseFunctionSample> ps =
                                gphase.Sample_p(-ray.d, u);
                            if (!ps || ps->pdf == 0)
                                terminated = true;
                            else {
                                // Update ray path state for indirect volume scattering
                                Float phaseFunctionWeight = ps->p / ps->pdf;
                                beta *= phaseFunctionWeight;
                                r_l = r_u / ps->pdf;
                                prevIntrContext = LightSampleContext(intr);
                                scattered = true;
                                ray.o = p;
                                ray.d = ps->wi;
                                specularBounce = false;
                                anyNonSpecularBounces = true;

                                guiding_addVolumeData(pathSegmentData, phaseFunctionWeight, ps->wi, ps->pdf, ps->meanCosine, survivalProb);
                            }
                        }
                        return false;

                    } else {
                        // Handle null scattering along ray path
                        SampledSpectrum sigma_n =
                            ClampZero(sigma_maj - mp.sigma_a - mp.sigma_s);
                        Float pdf = T_maj[lambda.ChannelIdx()] * sigma_n[lambda.ChannelIdx()];
                        beta *= T_maj * sigma_n / pdf;
                        transmittanceWeight *= T_maj * sigma_n / pdf;
                        if (pdf == 0) {
                            beta = SampledSpectrum(0.f);
                            transmittanceWeight = SampledSpectrum(0.f);
                        }
                        r_u *= T_maj * sigma_n / pdf;
                        r_l *= T_maj * sigma_maj / pdf;
                        return beta && r_u;
                    }
                });
            // Handle terminated, scattered, and unscattered medium rays
            if (terminated || !beta || !r_u)
                break;
            if (scattered)
                continue;

            transmittanceWeight *= T_maj / T_maj[lambda.ChannelIdx()];
            beta *= T_maj / T_maj[lambda.ChannelIdx()];
            r_u *= T_maj / T_maj[lambda.ChannelIdx()];
            r_l *= T_maj / T_maj[lambda.ChannelIdx()];
        }

        // Handle surviving unscattered rays
        guiding_addTransmittanceWeight(pathSegmentData, transmittanceWeight, lambda, colorSpace);

        // Add emitted light at volume path vertex or from the environment
        if (!si) {
            // Accumulate contributions from infinite light sources
            for (const auto &light : infiniteLights) {
                SampledSpectrum Le = light.Le(ray, lambda);
                if (depth == 0 || specularBounce) {
                    L += beta * Le / r_u.Average();
                    guiding_addInfiniteLightEmission(pathSegmentStorage, guidingInfiniteLightDistance, ray, Le, 1.0f, lambda, colorSpace);
                } else {
                    // Add infinite light contribution using both PDFs with MIS
                    Float lightPDF = lightSampler.PMF(prevIntrContext, light) *
                                light.PDF_Li(prevIntrContext, ray.d, true);
                    r_l *= lightPDF;
                    Float w_b = useNEE ? 1.0f / (r_u + r_l).Average() : 1.f;
                    L += beta * w_b * Le;
                    guiding_addInfiniteLightEmission(pathSegmentStorage, guidingInfiniteLightDistance, ray, Le, w_b, lambda, colorSpace);
                }
            }

            break;
        }
        // Incorporate emission from surface hit by ray
        SurfaceInteraction &isect = si->intr;
        SampledSpectrum Le = isect.Le(-ray.d, lambda);
        if (Le) {
            // Add contribution of emission from intersected surface
            if (depth == 0 || specularBounce) {
                L += beta * Le / r_u.Average();

                w = 1.0f;
                add_direct_contribution = true;
            } else {
                // Add surface light contribution using both PDFs with MIS
                Light areaLight(isect.areaLight);
                Float lightPDF = lightSampler.PMF(prevIntrContext, areaLight) *
                            areaLight.PDF_Li(prevIntrContext, ray.d, true);
                r_l *= lightPDF;
                // TODO add handling of survivial probability
                Float w_l = useNEE ? 1.0f / (r_u + r_l).Average() : 1.0f;
                L += beta * w_l * Le;
                w = w_l;
                add_direct_contribution = true;
            }
        }

        // Get BSDF and skip over medium boundaries
        BSDF bsdf = isect.GetBSDF(ray, lambda, camera, scratchBuffer, sampler);
        if (!bsdf) {
            isect.SkipIntersection(&ray, si->tHit);
            continue;
        }

        pathSegmentData = guiding_newSurfacePathSegment(pathSegmentStorage, ray, si);
        transmittanceWeight = SampledSpectrum(1.0f);

        if(add_direct_contribution)
        {
            guiding_addSurfaceEmission(pathSegmentData, Le, w, lambda, colorSpace);
        }
        add_direct_contribution = false;

        // Initialize _visibleSurf_ at first intersection
        if (depth == 0 && (visibleSurf || calculateImageSpaceGuidingBuffer)) {
            // Estimate BSDF's albedo
            // Define sample arrays _ucRho_ and _uRho_ for reflectance estimate
            constexpr int nRhoSamples = 16;
            const Float ucRho[nRhoSamples] = {
                0.75741637, 0.37870818, 0.7083487, 0.18935409, 0.9149363, 0.35417435,
                0.5990858,  0.09467703, 0.8578725, 0.45746812, 0.686759,  0.17708716,
                0.9674518,  0.2995429,  0.5083201, 0.047338516};
            const Point2f uRho[nRhoSamples] = {
                Point2f(0.855985, 0.570367), Point2f(0.381823, 0.851844),
                Point2f(0.285328, 0.764262), Point2f(0.733380, 0.114073),
                Point2f(0.542663, 0.344465), Point2f(0.127274, 0.414848),
                Point2f(0.964700, 0.947162), Point2f(0.594089, 0.643463),
                Point2f(0.095109, 0.170369), Point2f(0.825444, 0.263359),
                Point2f(0.429467, 0.454469), Point2f(0.244460, 0.816459),
                Point2f(0.756135, 0.731258), Point2f(0.516165, 0.152852),
                Point2f(0.180888, 0.214174), Point2f(0.898579, 0.503897)};

            const SampledSpectrum albedo = bsdf.rho(isect.wo, ucRho, uRho);
            const RGB albedoRGB = albedo.ToRGB(lambda, *colorSpace);

            if(visibleSurf)
                *visibleSurf = VisibleSurface(isect, albedo, lambda);

            cedSample.albedo = openpgl::cpp::Vector3f(albedoRGB[0], albedoRGB[1], albedoRGB[2]);
            cedSample.normal = openpgl::cpp::Vector3f(isect.n[0], isect.n[1], isect.n[2]);
            cedSample.SetSurfaceEvent(true);
        }

        // Terminate path if maximum depth reached
        if (depth++ >= maxDepth)
            break;

        ++surfaceInteractions;
        // Possibly regularize the BSDF
        if (regularize && anyNonSpecularBounces) {
            ++regularizedBSDFs;
            bsdf.Regularize();
        }

        // Guiding - Check if we can use guiding. If so intialize the guiding distribution
        Float v = sampler.Get1D();
        gbsdf.init(&bsdf, ray, si, v);
        if(guideRR && guideSurfaceRR) {
            adjointEstimate = gbsdf.OutgoingRadiance(-ray.d);
        }

        if (guideRR && depth > minRRDepth) {
            if(guideSurfaceRR) {
                survivalProb = specularBounce ? 0.95 : openpgl::cpp::util::GuidedRussianRoulette(OPGLVector3f(beta), OPGLVector3f(adjointEstimate), OPGLVector3f(pixelContributionEstimate), 0.1f);
            } else {
                survivalProb = 1.f;
            }
        }

        if (depth == 1 && visibleSurf && guiding_field->GetIteration() > 0) {
            visibleSurf->guidingData.id = gbsdf.getId();
        }

        // Sample illumination from lights to find attenuated path contribution
        if (useNEE && IsNonSpecular(bsdf.Flags())) {
            SampledSpectrum Ld = SampleLd(isect, &gbsdf, nullptr, 1.0f, lambda, sampler, r_u);
            L += beta * Ld;
            DCHECK(IsInf(L.y(lambda)) == false);

            // Guiding - add scattered contribution from NEE
            guiding_addScatteredDirectLight(pathSegmentData, Ld, lambda, colorSpace);
        }
        prevIntrContext = LightSampleContext(isect);

        // Sample BSDF to get new path direction
        //Vector3f wo = isect.wo;  // Note isect.wo does an explicit Normalize step.
        Vector3f wo = -ray.d; // Use -ray.d to be on par to GuidedPath
        Float u = sampler.Get1D();
        pstd::optional<BSDFSample> bs = gbsdf.Sample_f(wo, u, sampler.Get2D());
        if (!bs)
            break;

        rr_correction *= bs->pdf / bs->bsdfPdf;
        misPDF = bs->misPdf;
        // Update _beta_ and rescaled path probabilities for BSDF scattering
        bsdfWeight = bs->f * AbsDot(bs->wi, isect.shading.n) / bs->pdf;
        beta *= bsdfWeight;
        //if (bs->pdfIsProportional)
        //    r_l = r_u / bsdf.PDF(wo, bs->wi);
        //else
        //    r_l = r_u / bs->pdf;
        r_l = r_u / bs->misPdf;

        PBRT_DBG("%s\n", StringPrintf("Sampled BSDF, f = %s, pdf = %f -> beta = %s",
                                      bs->f, bs->pdf, beta)
                             .c_str());
        DCHECK(IsInf(beta.y(lambda)) == false);
        // Update volumetric integrator path state after surface scattering
        specularBounce = bs->IsSpecular();
        anyNonSpecularBounces |= !bs->IsSpecular();
        if (bs->IsTransmission())
            etaScale *= Sqr(bs->eta);
        ray = isect.SpawnRay(ray, bsdf, bs->wi, bs->flags, bs->eta);

        // Account for attenuated subsurface scattering, if applicable
/*
        BSSRDF bssrdf = isect.GetBSSRDF(ray, lambda, camera, scratchBuffer);
        if (bssrdf && bs->IsTransmission()) {
            // Sample BSSRDF probe segment to find exit point
            Float uc = sampler.Get1D();
            Point2f up = sampler.Get2D();
            pstd::optional<BSSRDFProbeSegment> probeSeg = bssrdf.SampleSp(uc, up);
            if (!probeSeg)
                break;

            // Sample random intersection along BSSRDF probe segment
            uint64_t seed = MixBits(FloatToBits(sampler.Get1D()));
            WeightedReservoirSampler<SubsurfaceInteraction> interactionSampler(seed);
            // Intersect BSSRDF sampling ray against the scene geometry
            Interaction base(probeSeg->p0, ray.time, Medium());
            while (true) {
                Ray r = base.SpawnRayTo(probeSeg->p1);
                if (r.d == Vector3f(0, 0, 0))
                    break;
                pstd::optional<ShapeIntersection> si = Intersect(r, 1);
                if (!si)
                    break;
                base = si->intr;
                if (si->intr.material == isect.material)
                    interactionSampler.Add(SubsurfaceInteraction(si->intr), 1.f);
            }

            if (!interactionSampler.HasSample())
                break;

            // Convert probe intersection to _BSSRDFSample_
            SubsurfaceInteraction ssi = interactionSampler.GetSample();
            BSSRDFSample bssrdfSample =
                bssrdf.ProbeIntersectionToSample(ssi, scratchBuffer);
            if (!bssrdfSample.Sp || !bssrdfSample.pdf)
                break;

            // Update path state for subsurface scattering
            Float pdf = interactionSampler.SampleProbability() * bssrdfSample.pdf[0];
            beta *= bssrdfSample.Sp / pdf;
            r_u *= bssrdfSample.pdf / bssrdfSample.pdf[0];
            SurfaceInteraction pi = ssi;
            pi.wo = bssrdfSample.wo;
            prevIntrContext = LightSampleContext(pi);
            // Possibly regularize subsurface BSDF
            BSDF &Sw = bssrdfSample.Sw;
            anyNonSpecularBounces = true;
            if (regularize) {
                ++regularizedBSDFs;
                Sw.Regularize();
            } else
                ++totalBSDFs;

            // Account for attenuated direct illumination subsurface scattering
            L += SampleLd(pi, &Sw, lambda, sampler, beta, r_u);

            // Sample ray for indirect subsurface scattering
            Float u = sampler.Get1D();
            pstd::optional<BSDFSample> bs = Sw.Sample_f(pi.wo, u, sampler.Get2D());
            if (!bs)
                break;
            beta *= bs->f * AbsDot(bs->wi, pi.shading.n) / bs->pdf;
            r_l = r_u / bs->pdf;
            // Don't increment depth this time...
            DCHECK(!IsInf(beta.y(lambda)));
            specularBounce = bs->IsSpecular();
            ray = RayDifferential(pi.SpawnRay(bs->wi));
        }
*/
        // Possibly terminate volumetric path with Russian roulette
        if (!beta)
            break;
        //SampledSpectrum rrBeta = beta * etaScale / r_u.Average();
        //        PBRT_DBG("%s\n",
        //         StringPrintf("etaScale %f -> rrBeta %s", etaScale, rrBeta).c_str());
        if (!guideRR && depth > minRRDepth) {
            const SampledSpectrum rrThroughputWeight = (beta / r_u.Average()) * rr_correction * etaScale;
            survivalProb = specularBounce ? 0.95 : openpgl::cpp::util::StandardThroughputBasedRussianRoulette(OPGLVector3f(rrThroughputWeight));
        }
        if (survivalProb < 1 && depth > minRRDepth) {
            Float q = std::max<Float>(0, 1 - survivalProb);
            if (sampler.Get1D() < q)
                break;
            beta /= 1 - q;
        }
        // Guiding - Add BSDF data to the current path segment
        guiding_addSurfaceData(pathSegmentData, bsdfWeight, bs->wi, bs->eta, bs->sampledRoughness, bs->pdf,
#ifdef OPENPGL_GUIDING_PDF_CACHES
                               bs->guidingPDF,
#endif
                               survivalProb, lambda, colorSpace);
    }

    pathLength << depth;

    if(calculateImageSpaceGuidingBuffer)
    {
#if defined(PBRT_RGB_RENDERING)
        RGB color = L.ToRGB(lambda, *colorSpace);
#else
        RGB color = sensor->ToSensorRGB(L, lambda);
#endif
        cedSample.contribution = openpgl::cpp::Vector3f(color[0], color[1], color[2]);
        imageSpaceGuidingBuffer->AddSample(openpgl::cpp::Point2i(pPixel[0], pPixel[1]), cedSample);
    }

    if (guideTraining)
    {
        //pathSegmentStorage->ValidateSegments();
        pathSegmentStorage->PropagateSamples(guiding_sampleStorage, true, true);
        pathSegmentStorage->Clear();
    }
    else
    {
        pathSegmentStorage->Clear();
    }
    return L;
}

SampledSpectrum GuidedVolPathIntegrator::SampleLd(const Interaction &intr, const GuidedBSDF *bsdf, const GuidedPhaseFunction *phase,
                                            const Float survivalProb, SampledWavelengths &lambda, Sampler sampler,
                                            SampledSpectrum r_p) const {
    // Estimate light-sampled direct illumination at _intr_
    // Initialize _LightSampleContext_ for volumetric light sampling
    LightSampleContext ctx;
    if (bsdf) {
        ctx = LightSampleContext(intr.AsSurface());
        // Try to nudge the light sampling position to correct side of the surface
        BxDFFlags flags = bsdf->Flags();
        if (IsReflective(flags) && !IsTransmissive(flags))
            ctx.pi = intr.OffsetRayOrigin(intr.wo);
        else if (IsTransmissive(flags) && !IsReflective(flags))
            ctx.pi = intr.OffsetRayOrigin(-intr.wo);

    } else
        ctx = LightSampleContext(intr);

    // Sample a light source using _lightSampler_
    Float u = sampler.Get1D();
    pstd::optional<SampledLight> sampledLight = lightSampler.Sample(ctx, u);
    Point2f uLight = sampler.Get2D();
    if (!sampledLight)
        return SampledSpectrum(0.f);
    Light light = sampledLight->light;
    DCHECK(light && sampledLight->p != 0);

    // Sample a point on the light source
    pstd::optional<LightLiSample> ls = light.SampleLi(ctx, uLight, lambda, true);
    if (!ls || !ls->L || ls->pdf == 0)
        return SampledSpectrum(0.f);
    Float p_l = sampledLight->p * ls->pdf;

    // Evaluate BSDF or phase function for light sample direction
    Float scatterPDF;
    SampledSpectrum f_hat;
    Vector3f wo = intr.wo, wi = ls->wi;
    if (bsdf) {
        // Update _f_hat_ and _scatterPDF_ accounting for the BSDF
        f_hat = bsdf->f(wo, wi) * AbsDot(wi, intr.AsSurface().shading.n);
        scatterPDF = survivalProb * bsdf->PDF(wo, wi);

    } else {
        // Update _f_hat_ and _scatterPDF_ accounting for the phase function
        CHECK(intr.IsMediumInteraction());
        //PhaseFunction phase = intr.AsMedium().phase;
        f_hat = SampledSpectrum(phase->p(wo, wi));
        scatterPDF = survivalProb * phase->PDF(wo, wi);
    }
    if (!f_hat)
        return SampledSpectrum(0.f);

    // Declare path state variables for ray to light source
    Ray lightRay = intr.SpawnRayTo(ls->pLight);
    SampledSpectrum T_ray(1.f), r_l(1.f), r_u(1.f);
    RNG rng(Hash(lightRay.o), Hash(lightRay.d));

    while (lightRay.d != Vector3f(0, 0, 0)) {
        // Trace ray through media to estimate transmittance
        pstd::optional<ShapeIntersection> si = Intersect(lightRay, 1 - ShadowEpsilon);
        // Handle opaque surface along ray's path
        if (si && si->intr.material)
            return SampledSpectrum(0.f);
        // Update transmittance for current ray segment
        if (lightRay.medium) {
            Float tMax = si ? si->tHit : (1 - ShadowEpsilon);
            Float u = rng.Uniform<Float>();
            SampledSpectrum T_maj =
                SampleT_maj(lightRay, tMax, u, rng, lambda,
                            [&](Point3f p, MediumProperties mp, SampledSpectrum sigma_maj,
                                SampledSpectrum T_maj) {
                                // Update ray transmittance estimate at sampled point
                                // Update _T_ray_ and PDFs using ratio-tracking estimator
                                SampledSpectrum sigma_n =
                                    ClampZero(sigma_maj - mp.sigma_a - mp.sigma_s);
                                Float pdf = T_maj[lambda.ChannelIdx()] * sigma_maj[lambda.ChannelIdx()];
                                T_ray *= T_maj * sigma_n / pdf;
                                r_l *= T_maj * sigma_maj / pdf;
                                r_u *= T_maj * sigma_n / pdf;

                                // Possibly terminate transmittance computation using
                                // Russian roulette
                                SampledSpectrum Tr = T_ray / (r_l + r_u).Average();
                                if (Tr.MaxComponentValue() < 0.05f) {
                                    Float q = 0.75f;
                                    if (rng.Uniform<Float>() < q)
                                        T_ray = SampledSpectrum(0.);
                                    else
                                        T_ray /= 1 - q;
                                }

                                if (!T_ray)
                                    return false;
                                return true;
                            });
            // Update transmittance estimate for final segment
            T_ray *= T_maj / T_maj[lambda.ChannelIdx()];
            r_l *= T_maj / T_maj[lambda.ChannelIdx()];
            r_u *= T_maj / T_maj[lambda.ChannelIdx()];
        }
        // Generate next ray segment or return final transmittance
        if (!T_ray)
            return SampledSpectrum(0.f);
        if (!si)
            break;
        lightRay = si->intr.SpawnRayTo(ls->pLight);
    }
    // Return path contribution function estimate for direct lighting
    r_l *= r_p * p_l;
    r_u *= r_p * scatterPDF;
    if (IsDeltaLight(light.Type()))
        return f_hat * T_ray * ls->L / r_l.Average();
    else
        return f_hat * T_ray * ls->L / (r_l + r_u).Average();
}

std::string GuidedVolPathIntegrator::ToString() const {
    return StringPrintf(
        "[ GuidedVolPathIntegrator maxDepth: %d lightSampler: %s regularize: %s ]", maxDepth,
        lightSampler, regularize);
}

std::unique_ptr<GuidedVolPathIntegrator> GuidedVolPathIntegrator::Create(
    const ParameterDictionary &parameters, const RGBColorSpace *colorSpace, Camera camera, Sampler sampler,
    Primitive aggregate, std::vector<Light> lights, const FileLoc *loc) {
    int maxDepth = parameters.GetOneInt("maxdepth", 5);
    int minRRDepth = parameters.GetOneInt("minrrdepth", 1);
    bool useNEE = parameters.GetOneBool("usenee", true);
    GuidingSettings settings;
    settings.knnLookup = parameters.GetOneBool("knnlookup", true);
    settings.guideSurface = parameters.GetOneBool("surfaceguiding", true);
    settings.guideVolume = parameters.GetOneBool("volumeguiding", true);
    settings.guideRR = parameters.GetOneBool("rrguiding", false);
    settings.guideSurfaceRR = parameters.GetOneBool("surfacerrguiding", true);
    settings.guideVolumeRR = parameters.GetOneBool("volumerrguiding", true);

    settings.enableGuiding = settings.guideSurface || settings.guideVolume;
    std::string strSurfaceGuidingType = parameters.GetOneString("surfaceguidingtype", "ris");
    settings.surfaceGuidingType = strSurfaceGuidingType == "mis" ? EGuideMIS : EGuideRIS;
    std::string strVolumeGuidingType = parameters.GetOneString("volumeguidingtype", "mis");
    settings.volumeGuidingType = strVolumeGuidingType == "mis" ? EGuideMIS : EGuideRIS;

    settings.storeGuidingCache = parameters.GetOneBool("storeGuidingCache", false);
    settings.loadGuidingCache = parameters.GetOneBool("loadGuidingCache", false);
    settings.guidingCacheFileName = parameters.GetOneString("guidingCacheFileName", "");

    settings.storeContributionEstimate = parameters.GetOneBool("storeContributionEstimate", false);
    settings.loadContributionEstimate = parameters.GetOneBool("loadContributionEstimate", false);
    settings.contributionEstimateFileName = parameters.GetOneString("contributionEstimateFileName", "");

    std::string lightStrategy = parameters.GetOneString("lightsampler", "bvh");
    bool regularize = parameters.GetOneBool("regularize", false);
    return std::make_unique<GuidedVolPathIntegrator>(maxDepth, minRRDepth, useNEE, settings, colorSpace, camera, sampler, aggregate,
                                               lights, lightStrategy, regularize);
}

}
