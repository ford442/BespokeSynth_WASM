// BespokeSynth WASM - Live analyzer FFT (WebGPU compute)
// WebGPU Shading Language (WGSL)
//
// Embedded at build time by scripts/embed_shaders.cmake. One workgroup runs a
// Hann-windowed radix-2 FFT of the newest FFT_SIZE samples in workgroup memory
// and writes bin magnitudes for fs_live_spectrum (render2d.wgsl). This mirrors
// AudioAnalysis::computeSpectrum(), which stays as the WebGL2 / CPU fallback.
//
// FFT_SIZE must equal AudioAnalysis::kFftSize and be 2 * WORKGROUP_SIZE (each
// invocation owns one butterfly per stage).

const FFT_SIZE: u32 = 512u;
const FFT_LOG2: u32 = 9u;
const WORKGROUP_SIZE: u32 = 256u;
const TWO_PI: f32 = 6.28318530717958647692;
// Same scaling as dsp::magnitude(..., gain = 0.004, ceiling = 1.0) on the CPU path.
const MAGNITUDE_GAIN: f32 = 0.004;
const MAGNITUDE_CEILING: f32 = 1.0;

struct AnalyzerParams {
    bins: u32,
    padding0: u32,
    padding1: u32,
    padding2: u32,
};

@group(0) @binding(0) var<storage, read> samples: array<f32>;
@group(0) @binding(1) var<storage, read_write> spectrum: array<f32>;
@group(0) @binding(2) var<uniform> params: AnalyzerParams;

var<workgroup> re: array<f32, FFT_SIZE>;
var<workgroup> im: array<f32, FFT_SIZE>;

fn hann(i: u32) -> f32 {
    return 0.5 - 0.5 * cos(TWO_PI * f32(i) / f32(FFT_SIZE - 1u));
}

@compute @workgroup_size(256)
fn cs_spectrum(@builtin(local_invocation_index) tid: u32) {
    // Load two windowed samples per invocation into bit-reversed slots.
    let total = arrayLength(&samples);
    let start = total - FFT_SIZE;
    for (var k = 0u; k < 2u; k = k + 1u) {
        let i = tid + k * WORKGROUP_SIZE;
        let dst = reverseBits(i) >> (32u - FFT_LOG2);
        re[dst] = samples[start + i] * hann(i);
        im[dst] = 0.0;
    }
    workgroupBarrier();

    // In-place decimation-in-time butterflies, one per invocation per stage.
    var half = 1u;
    for (var stage = 0u; stage < FFT_LOG2; stage = stage + 1u) {
        let j = tid % half;
        let i0 = (tid / half) * half * 2u + j;
        let i1 = i0 + half;
        let phase = -TWO_PI * f32(j) / f32(half * 2u);
        let wr = cos(phase);
        let wi = sin(phase);
        let tr = re[i1] * wr - im[i1] * wi;
        let ti = re[i1] * wi + im[i1] * wr;
        let ur = re[i0];
        let ui = im[i0];
        re[i0] = ur + tr;
        im[i0] = ui + ti;
        re[i1] = ur - tr;
        im[i1] = ui - ti;
        half = half * 2u;
        workgroupBarrier();
    }

    let bins = min(params.bins, FFT_SIZE / 2u);
    if (tid < bins) {
        let mag = sqrt(re[tid] * re[tid] + im[tid] * im[tid]) * MAGNITUDE_GAIN;
        spectrum[tid] = min(mag, MAGNITUDE_CEILING);
    }
}
