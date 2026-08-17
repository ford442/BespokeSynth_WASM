import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import { connectWebMidi, type MidiConnectResult } from '../midi';

export function setupMidiPanel(
  headerControls: Element,
  getModule: () => BespokeSynthModule | null,
): void {
  const wrap = document.createElement('div');
  wrap.className = 'midi-control';

  const midiBtn = document.createElement('button');
  midiBtn.id = 'midiBtn';
  midiBtn.className = 'btn';
  midiBtn.textContent = 'MIDI';
  midiBtn.title = 'Connect browser MIDI inputs';
  midiBtn.setAttribute('aria-haspopup', 'true');
  midiBtn.setAttribute('aria-expanded', 'false');

  const popover = document.createElement('div');
  popover.id = 'midiPopover';
  popover.className = 'midi-popover';
  popover.hidden = true;
  popover.setAttribute('role', 'status');
  popover.setAttribute('aria-live', 'polite');

  let connecting = false;

  const setPopoverOpen = (open: boolean): void => {
    popover.hidden = !open;
    midiBtn.setAttribute('aria-expanded', open ? 'true' : 'false');
  };

  const updateMidiButtonLabel = (result: MidiConnectResult): void => {
    switch (result.state) {
      case 'connected':
        midiBtn.textContent = `MIDI ${result.connectedCount}`;
        break;
      case 'no-devices':
        midiBtn.textContent = 'MIDI 0';
        break;
      case 'denied':
        midiBtn.textContent = 'MIDI denied';
        break;
      case 'timeout':
        midiBtn.textContent = 'MIDI timeout';
        break;
      case 'unsupported':
      case 'insecure':
        midiBtn.textContent = 'MIDI N/A';
        break;
      default:
        midiBtn.textContent = 'MIDI error';
    }
  };

  const renderMidiPopover = (result: MidiConnectResult, pending = false): void => {
    popover.replaceChildren();
    const status = document.createElement('div');
    status.className = `midi-popover-status midi-popover-status--${pending ? 'pending' : result.state}`;
    status.textContent = result.message;
    popover.appendChild(status);

    if (result.inputs.length > 0) {
      const heading = document.createElement('div');
      heading.className = 'midi-popover-heading';
      heading.textContent = 'Detected inputs';
      const list = document.createElement('ul');
      list.className = 'midi-popover-inputs';
      for (const input of result.inputs) {
        const item = document.createElement('li');
        item.textContent = input.name;
        list.appendChild(item);
      }
      popover.append(heading, list);
    }
  };

  midiBtn.addEventListener('click', async (event) => {
    event.stopPropagation();
    if (connecting) return;
    const module = getModule();
    if (!module) return;

    connecting = true;
    midiBtn.disabled = true;
    midiBtn.textContent = 'MIDI…';
    renderMidiPopover(
      {
        state: 'error',
        inputs: [],
        connectedCount: 0,
        message: 'Requesting MIDI access…',
      },
      true,
    );
    setPopoverOpen(true);

    const result = await connectWebMidi(module);
    connecting = false;
    midiBtn.disabled = false;
    renderMidiPopover(result);
    updateMidiButtonLabel(result);
    setPopoverOpen(true);
  });

  document.addEventListener('click', (event) => {
    if (!wrap.contains(event.target as Node)) setPopoverOpen(false);
  });

  wrap.append(midiBtn, popover);
  headerControls.appendChild(wrap);
}
