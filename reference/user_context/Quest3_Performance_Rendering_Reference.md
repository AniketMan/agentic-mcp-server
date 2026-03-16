# Quest 3 Performance Settings and Rendering Techniques Reference

Source: Meta's official `Unreal-PerformanceSettings` and `Unreal-RenderingTechniques` sample projects.
Engine: Oculus fork of UE 5.6 (`oculus-5.6.1-release-1.115.0-v83.0`)

---

## Optimal DefaultEngine.ini Settings for Quest 3

These are Meta's recommended settings extracted from their sample projects. Apply these to any new Quest 3 UE project.

### Renderer Settings

```ini
[/Script/Engine.RendererSettings]
; Forward shading is required for mobile VR
r.ForwardShading=True
r.VertexFoggingForOpaque=True

; Mobile HDR off for Quest
r.MobileHDR=False

; MSAA 4x is the standard for Quest VR
r.Mobile.AntiAliasing=3
r.MSAACount=4
r.AntiAliasingMethod=0

; Instanced stereo + mobile multi-view for VR performance
vr.InstancedStereo=True
vr.MobileMultiView=True

; Application SpaceWarp support (renders at half rate, synthesizes frames)
vr.SupportMobileSpaceWarp=True

; Static lighting - bake everything possible
r.AllowStaticLighting=True

; Disable expensive post-processing effects
r.DefaultFeature.Bloom=False
r.DefaultFeature.AmbientOcclusion=False
r.DefaultFeature.AutoExposure=False
r.DefaultFeature.MotionBlur=False
r.DefaultFeature.LensFlare=False

; Disable features not supported on mobile
r.Lumen.HardwareRayTracing=False
r.RayTracing=False
r.Shadow.Virtual.Enable=1
r.Nanite.ProjectEnabled=True
r.SeparateTranslucency=False

; Distance field shadows for mobile
r.Mobile.AllowDistanceFieldShadows=True
r.GenerateMeshDistanceFields=True

; Texture streaming on
r.TextureStreaming=True

; VRS (Variable Rate Shading) support
r.VRS.Support=True

; sRGB encoding on mobile
r.Mobile.UseHWsRGBEncoding=True

; Propagate alpha for passthrough compositing
r.PostProcessing.PropagateAlpha=True

; Custom depth for interaction highlights
r.CustomDepth=1
```

### OculusXR HMD Settings

```ini
[/Script/OculusXRHMD.OculusXRHMDRuntimeSettings]
; Dynamic resolution - auto-adjusts render scale based on GPU load
bDynamicResolution=True

; Color space
ColorSpace=P3

; OpenXR API
XrApi=OVRPluginOpenXR

; CPU/GPU performance levels
SuggestedCpuPerfLevel=SustainedLow
SuggestedGpuPerfLevel=SustainedHigh

; Fixed Foveated Rendering - reduces pixels at screen edges
FoveatedRenderingMethod=FixedFoveatedRendering
FoveatedRenderingLevel=Medium
bDynamicFoveatedRendering=True

; Passthrough support
bInsightPassthroughEnabled=True

; Hand tracking (set to ControllersAndHands for hand tracking support)
HandTrackingSupport=ControllersOnly

; Composites depth for proper occlusion
bCompositesDepth=True

; Target Quest 3
+SupportedDevices=Quest3

; Processor favor - balance CPU and GPU equally by default
ProcessorFavor=FavorEqually
```

### Android Runtime Settings

```ini
[/Script/AndroidRuntimeSettings.AndroidRuntimeSettings]
; Minimum SDK for Quest 3
MinSDKVersion=32
TargetSDKVersion=32

; Package for Meta Quest
bPackageForMetaQuest=True

; Vulkan only (no ES3.1)
bSupportsVulkanSM5=False
bBuildForES31=False

; Supported devices manifest
ExtraApplicationSettings=<meta-data android:name="com.oculus.supportedDevices" android:value="quest3" />
```

### Hardware Targeting

```ini
[/Script/HardwareTargeting.HardwareTargetingSettings]
TargetedHardwareClass=Mobile
AppliedTargetedHardwareClass=Mobile
DefaultGraphicsPerformance=Scalable
AppliedDefaultGraphicsPerformance=Scalable
```

---

## Performance Techniques

### Dynamic Resolution (Recommended)

Dynamic resolution automatically adjusts render scale based on GPU utilization. It increases resolution when the GPU is underutilized and decreases it when the GPU is fully loaded. This is the single most effective performance/quality tradeoff.

- Enabled via `bDynamicResolution=True` in OculusXR settings
- Control range with `PixelDensityMin` and `PixelDensityMax`
- Docs: https://developers.meta.com/horizon/documentation/unreal/dynamic-resolution-unreal

### Fixed Foveated Rendering (FFR)

Renders fewer pixels at the edges of the screen where the user is not looking. No eye tracking needed (fixed pattern).

Levels: Off, Low, Medium, High, HighTop
- `FoveatedRenderingLevel=Medium` is a good default
- `bDynamicFoveatedRendering=True` adjusts FFR level based on GPU load
- Docs: https://developers.meta.com/horizon/documentation/unreal/unreal-ffr

### Application SpaceWarp (AppSW)

Renders at half framerate (36 FPS) and synthesizes intermediate frames to output 72 FPS. Uses motion vectors and depth buffer for frame extrapolation.

- Enable: `vr.SupportMobileSpaceWarp=True`
- Best for: GPU-heavy scenes where you can't hit 72 FPS natively
- Tradeoff: Slight artifacts on fast-moving objects, but massive GPU savings
- Docs: https://developers.meta.com/horizon/documentation/unreal/os-app-spacewarp

### CPU/GPU Performance Levels

Control clock speeds of the Quest CPU and GPU. Higher levels = more power = more heat = shorter battery.

| Level | Description |
|-------|-------------|
| PowerSaving | Lowest clocks, longest battery |
| SustainedLow | Low clocks, good battery |
| SustainedHigh | High clocks, moderate battery |
| Boost | Maximum clocks, short battery, may throttle |

Default recommendation: CPU=SustainedLow, GPU=SustainedHigh (GPU-bound apps)

### Quest 3: Trading CPU for GPU Levels

Quest 3 only. If your app is GPU-bound, you can trade one CPU level for one GPU level:

```xml
<meta-data android:name="com.oculus.trade_cpu_for_gpu_amount" android:value="1" />
```

This is a manifest setting, not changeable at runtime.

### MSAA (Multisampled Anti-Aliasing)

MSAA 4x is the standard for Quest VR. It reduces edge aliasing efficiently on mobile GPUs.

- `r.MSAACount=4`
- `r.Mobile.AntiAliasing=3`
- Cost is moderate on Adreno 740 (Quest 3 GPU)
- Analysis: https://developers.meta.com/horizon/documentation/native/android/mobile-msaa-analysis/

---

## Rendering Techniques

### Cascaded Shadow Maps (CSM)

Dynamic shadows for movable lights. Lights must be set to Movable (not Stationary) for CSM to work on device.

- `r.Shadow.CSM.MaxMobileCascades=4`
- `r.Shadow.CSMCaching=True` (cache static shadow casters)
- Shadows may disappear with certain camera/light angles if far distance is too large

### Distance Field Baked Shadows

High-quality precomputed hard-edged shadows for stationary/static lights.

- Light must be Stationary
- Enable `r.Mobile.AllowDistanceFieldShadows=True`
- Enable `r.GenerateMeshDistanceFields=True`
- Enable "Support Distance Field Shadows" in Project Settings > Mobile Shader Permutation Reduction

### Color Grading with LUTs

Apply look-up tables (LUTs) to adjust scene color grading. Low-cost post-processing effect suitable for mobile VR.

### Stereo Portal Rendering

Two methods for rendering portals in VR:

1. **Parallax-corrected cubemap** — prebaked, cheap, but has warping and no stereo depth
2. **Stereo render targets** — renders scene from each eye's perspective through the portal. Convincing but expensive (doubles render cost for portal view)

### Text Rendering in VR

- Avoid premultiplied alpha for transparent text
- VR stereo layers can render high-quality text/textures at native resolution
- Texture filtering settings significantly affect text readability

---

## Texture LOD Groups (from RenderingTechniques)

Custom texture groups for controlling filtering per-asset type:

| Group | Filter | Mips | Use Case |
|-------|--------|------|----------|
| Project01 | Bilinear | No Mips | UI, text, sharp textures |
| Project02 | Bilinear | Mipmapped | General 2D |
| Project03 | Trilinear | Mipmapped | Smooth gradients |
| Project04 | Anisotropic | Mipmapped | Floors, walls at angle |
| Project05 | Anisotropic | LOD Bias -1 | High detail surfaces |

---

## Key CVars for Quest 3 Optimization

```
r.MobileContentScaleFactor=1.0          ; Render at native resolution
r.BloomQuality=0                         ; Disable bloom
r.DepthOfFieldQuality=0                  ; Disable DOF
r.LightShaftQuality=0                    ; Disable light shafts
r.RefractionQuality=0                    ; Disable refraction (or 1 for basic)
r.ShadowQuality=2                        ; Medium shadows
r.HZBOcclusion=0                         ; Disable HZB (slower with tiled rendering)
r.EarlyZPass=0                           ; Skip depth prepass on mobile
r.TranslucentLightingVolume=0            ; Needs geometry shader (not available)
r.AllowPointLightCubemapShadows=0        ; Needs geometry shader
r.PostProcessAAQuality=0                 ; Disable temporal AA post-process
r.Mobile.AmbientOcclusion=False          ; Disable mobile AO
r.Mobile.DBuffer=False                   ; Disable decal buffer on mobile
r.Mobile.ScreenSpaceReflections=False    ; Disable SSR on mobile
```

---

## Sources

- https://github.com/oculus-samples/Unreal-PerformanceSettings
- https://github.com/oculus-samples/Unreal-RenderingTechniques
- https://developers.meta.com/horizon/documentation/unreal/os-render-scale/
- https://developers.meta.com/horizon/documentation/unreal/unreal-ffr
- https://developers.meta.com/horizon/documentation/unreal/os-app-spacewarp
- https://developers.meta.com/horizon/documentation/unreal/os-cpu-gpu-levels
- https://developers.meta.com/horizon/documentation/unreal/gpu-improved-algorithms/
