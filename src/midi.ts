import type { BespokeSynthModule } from '../wasm/types/bespoke-synth';

interface MidiMessageEvent extends Event { data: Uint8Array; }
interface MidiInput extends EventTarget { onmidimessage: ((event: MidiMessageEvent) => void) | null; }
interface MidiAccess { inputs: Map<string, MidiInput>; }

export async function connectWebMidi(module: BespokeSynthModule): Promise<number> {
  const request = (navigator as Navigator & { requestMIDIAccess?: () => Promise<MidiAccess> }).requestMIDIAccess;
  if (!request) throw new Error('Web MIDI is not supported by this browser');
  const access = await request.call(navigator);
  let connected = 0;
  access.inputs.forEach((input) => {
    input.onmidimessage = (event) => {
      const [status, data1, data2] = event.data;
      const command = status & 0xf0;
      const channel = status & 0x0f;
      if (command === 0x90 && data2 > 0) module._bespoke_midi_note_on(channel, data1, data2 / 127);
      else if (command === 0x80 || (command === 0x90 && data2 === 0)) module._bespoke_midi_note_off(channel, data1);
      else if (command === 0xb0) module._bespoke_midi_cc(channel, data1, data2 / 127);
    };
    connected++;
  });
  return connected;
}
