import type { BespokeSynthModule } from '../../wasm/types/bespoke-synth';
import type { LiveInstrument } from '../app/liveInstrument';
import { connectWebMidi, type MidiConnectResult } from '../midi';

function heading(text: string): HTMLElement {
  const el = document.createElement('div');
  el.className = 'midi-popover-heading';
  el.textContent = text;
  return el;
}

function row(label: string, control: HTMLElement): HTMLElement {
  const wrap = document.createElement('label');
  wrap.className = 'midi-popover-row';
  const text = document.createElement('span');
  text.textContent = label;
  wrap.append(text, control);
  return wrap;
}

export function setupMidiPanel(
  headerControls: Element,
  getModule: () => BespokeSynthModule | null,
  live?: LiveInstrument,
): void {
  const wrap = document.createElement('div');
  wrap.className = 'midi-control';

  const midiBtn = document.createElement('button');
  midiBtn.id = 'midiBtn';
  midiBtn.className = 'btn';
  midiBtn.textContent = 'MIDI';
  midiBtn.title = 'MIDI inputs/outputs, channel, QWERTY piano, and CC learn';
  midiBtn.setAttribute('aria-haspopup', 'true');
  midiBtn.setAttribute('aria-expanded', 'false');

  const popover = document.createElement('div');
  popover.id = 'midiPopover';
  popover.className = 'midi-popover';
  popover.hidden = true;
  popover.setAttribute('role', 'dialog');
  popover.setAttribute('aria-label', 'MIDI settings');

  const status = document.createElement('div');
  status.setAttribute('role', 'status');
  status.setAttribute('aria-live', 'polite');

  // Note channel filter (graph-wide; stored in the patch as midi.note_channel).
  const channelSelect = document.createElement('select');
  channelSelect.id = 'midiChannelSelect';
  channelSelect.className = 'renderer-select';
  channelSelect.innerHTML =
    '<option value="-1">Omni</option>' +
    Array.from({ length: 16 }, (_, i) => `<option value="${i}">Ch ${i + 1}</option>`).join('');
  channelSelect.addEventListener('change', () => {
    getModule()?._bespoke_set_midi_note_channel(Number(channelSelect.value));
  });

  const outputSelect = document.createElement('select');
  outputSelect.id = 'midiOutputSelect';
  outputSelect.className = 'renderer-select';
  outputSelect.addEventListener('change', () => {
    live?.midiOut.selectOutput(outputSelect.value || null);
  });

  const pianoToggle = document.createElement('input');
  pianoToggle.type = 'checkbox';
  pianoToggle.id = 'qwertyPianoToggle';
  pianoToggle.checked = live?.piano.enabled ?? false;
  pianoToggle.addEventListener('change', () => live?.piano.setEnabled(pianoToggle.checked));

  const pianoInfo = document.createElement('div');
  pianoInfo.className = 'midi-popover-hint';
  const renderPianoInfo = () => {
    if (!live) return;
    const { octave, velocity } = live.piano.getState();
    pianoInfo.textContent = `Z–M and Q–P play from C${octave} · −/= octave · [ ] velocity (${velocity.toFixed(1)}) · Shift accents`;
  };
  live?.onPianoChange(() => {
    pianoToggle.checked = live.piano.enabled;
    renderPianoInfo();
  });
  renderPianoInfo();

  const learnHint = document.createElement('div');
  learnHint.className = 'midi-popover-hint';
  learnHint.textContent = 'CC learn: Shift-click a control, then move a hardware knob. Esc cancels, Delete unmaps.';

  const inputsList = document.createElement('ul');
  inputsList.className = 'midi-popover-inputs';

  let connecting = false;
  let lastResult: MidiConnectResult | null = null;

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

  const renderStatus = (message: string, state: string): void => {
    status.className = `midi-popover-status midi-popover-status--${state}`;
    status.textContent = message;
  };

  const renderPorts = (result: MidiConnectResult | null): void => {
    inputsList.replaceChildren(
      ...(result?.inputs ?? []).map((input) => {
        const item = document.createElement('li');
        item.textContent = input.name;
        return item;
      }),
    );
    const selected = live?.midiOut.selectedOutputId() ?? '';
    const outputs = live?.midiOut.listOutputs() ?? result?.outputs ?? [];
    outputSelect.replaceChildren(new Option('None', ''), ...outputs.map((o) => new Option(o.name, o.id)));
    outputSelect.value = outputs.some((o) => o.id === selected) ? selected : '';
    outputSelect.disabled = outputs.length === 0;
  };

  const renderPopover = (): void => {
    const module = getModule();
    const channel = module?._bespoke_get_midi_note_channel() ?? -1;
    channelSelect.value = String(channel);
    renderPorts(lastResult);
    const clock = live?.isClockRunning()
      ? [heading('Clock out'), Object.assign(document.createElement('div'), {
          className: 'midi-popover-hint',
          textContent: 'Sending 24 PPQ clock + start/stop to the selected output (?midiClock=1).',
        })]
      : [];
    popover.replaceChildren(
      status,
      ...(lastResult && lastResult.inputs.length > 0 ? [heading('Detected inputs'), inputsList] : []),
      heading('Routing'),
      row('Note channel', channelSelect),
      row('Output', outputSelect),
      ...clock,
      heading('Computer keyboard'),
      row('QWERTY piano', pianoToggle),
      pianoInfo,
      heading('Learn'),
      learnHint,
    );
  };

  midiBtn.addEventListener('click', async (event) => {
    event.stopPropagation();
    if (!popover.hidden && !connecting) {
      setPopoverOpen(false);
      return;
    }
    const module = getModule();
    if (!module) return;

    renderPopover();
    setPopoverOpen(true);
    if (connecting || lastResult?.state === 'connected') return;

    connecting = true;
    midiBtn.disabled = true;
    midiBtn.textContent = 'MIDI…';
    renderStatus('Requesting MIDI access…', 'pending');

    const result = await connectWebMidi(module, live ? (eventTime) => live.noteTime(eventTime) : undefined);
    connecting = false;
    midiBtn.disabled = false;
    lastResult = result;
    renderStatus(result.message, result.state);
    updateMidiButtonLabel(result);
    renderPopover();
    setPopoverOpen(true);
  });

  document.addEventListener('click', (event) => {
    if (!wrap.contains(event.target as Node)) setPopoverOpen(false);
  });

  renderStatus('Click MIDI to connect devices.', 'pending');
  wrap.append(midiBtn, popover);
  headerControls.appendChild(wrap);
}
