import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';

export interface InputCaptureHandle {
  stop: () => void;
  running: () => boolean;
}

export async function startInputCapture(module: BespokeSynthModule): Promise<InputCaptureHandle> {
  const stream = await navigator.mediaDevices.getUserMedia({ audio: true, video: false });
  const context = new AudioContext();
  const source = context.createMediaStreamSource(stream);
  const processor = context.createScriptProcessor(512, 1, 1);
  processor.onaudioprocess = (event) => {
    const input = event.inputBuffer.getChannelData(0);
    const bytes = input.byteLength;
    const ptr = module._malloc(bytes);
    module.HEAPF32.set(input, ptr >> 2);
    module._bespoke_push_input_audio(ptr, input.length);
    module._free(ptr);
  };
  const mute = context.createGain();
  mute.gain.value = 0;
  source.connect(processor);
  processor.connect(mute);
  mute.connect(context.destination);

  let running = true;
  return {
    running: () => running,
    stop: () => {
      if (!running) return;
      running = false;
      processor.disconnect();
      source.disconnect();
      stream.getTracks().forEach((track) => track.stop());
      void context.close();
    },
  };
}
