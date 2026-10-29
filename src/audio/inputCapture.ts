import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';

export interface InputCaptureHandle {
  stop: () => void;
  running: () => boolean;
}

/** Frames per worklet -> main-thread message; matches the 512-frame buffer the old ScriptProcessor used. */
const CHUNK_FRAMES = 512;

function micCaptureUrl(): string {
  return new URL('audio/micCapture.worklet.js', window.location.href).toString();
}

/**
 * Captures the default microphone with an AudioWorklet and feeds mono frames
 * into the C++ InputAudioBus (`bespoke_push_input_audio`).
 */
export async function startInputCapture(module: BespokeSynthModule): Promise<InputCaptureHandle> {
  const stream = await navigator.mediaDevices.getUserMedia({ audio: true, video: false });
  const context = new AudioContext();
  let node: AudioWorkletNode;
  let source: MediaStreamAudioSourceNode;
  try {
    await context.audioWorklet.addModule(micCaptureUrl());
    node = new AudioWorkletNode(context, 'bespoke-mic-capture', {
      numberOfInputs: 1,
      numberOfOutputs: 1,
      channelCount: 1,
      channelCountMode: 'explicit',
      outputChannelCount: [1],
      processorOptions: { chunkFrames: CHUNK_FRAMES },
    });
    source = context.createMediaStreamSource(stream);
    if (context.state === 'suspended') await context.resume();
  } catch (error) {
    stream.getTracks().forEach((track) => track.stop());
    void context.close();
    throw error;
  }

  // One staging buffer for the capture's lifetime instead of a malloc/free per chunk.
  const stagingPtr = module._malloc(CHUNK_FRAMES * 4);
  let running = true;
  node.port.onmessage = (event: MessageEvent<{ type: string; samples?: Float32Array }>) => {
    const samples = event.data.samples;
    if (!running || event.data.type !== 'chunk' || !samples || samples.length > CHUNK_FRAMES) return;
    module.HEAPF32.set(samples, stagingPtr >> 2);
    module._bespoke_push_input_audio(stagingPtr, samples.length);
  };

  source.connect(node);
  // The node never writes its output (it stays silent); the connection only
  // keeps the browser pulling the graph so process() keeps being called.
  node.connect(context.destination);

  return {
    running: () => running,
    stop: () => {
      if (!running) return;
      running = false;
      node.port.postMessage({ type: 'stop' });
      node.port.onmessage = null;
      source.disconnect();
      node.disconnect();
      stream.getTracks().forEach((track) => track.stop());
      void context.close();
      module._free(stagingPtr);
    },
  };
}
