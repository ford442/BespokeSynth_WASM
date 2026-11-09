import { audioBackendLabel, type AudioStatus, type AudioStatusMonitor } from '../audio/audioStatus';

const LEVEL_CLASSES = ['audio-status--idle', 'audio-status--ok', 'audio-status--warn', 'audio-status--error'];

function describe(status: AudioStatus, backendNote: string | null): string {
  const lines = [status.message, `Backend: ${audioBackendLabel(status.backend)}`];
  if (backendNote) lines.push(backendNote);
  lines.push(`AudioContext: ${status.contextState ?? 'not created yet'}`);
  if (status.clockAdvancing !== null) lines.push(`Audio clock: ${status.clockAdvancing ? 'advancing' : 'stalled'}`);
  lines.push(
    `Output devices: ${status.outputDevices === null ? 'not reported by this browser' : String(status.outputDevices)}`,
  );
  return lines.join('\n');
}

function shortLabel(status: AudioStatus): string {
  switch (status.state) {
    case 'running':
      return 'Audio on';
    case 'idle':
    case 'starting':
      return 'Audio idle';
    case 'suspended':
      return 'Audio suspended';
    case 'interrupted':
      return 'Audio interrupted';
    case 'stalled':
    case 'no-device':
      return 'No audio output';
    case 'closed':
      return 'Audio closed';
    default:
      return 'Audio error';
  }
}

/**
 * Header chip with the real output state (text and colour, never colour alone) and a banner over
 * the canvas when sound cannot come out. `backendNote` explains a fallback in the chip tooltip.
 */
export function setupAudioStatusIndicator(monitor: AudioStatusMonitor, backendNote: string | null = null): void {
  const controls = document.querySelector('#header .controls');
  const container = document.getElementById('canvas-container');
  if (!controls || !container) return;

  const chip = document.createElement('span');
  chip.id = 'audioStatus';
  chip.className = 'audio-status';
  chip.setAttribute('role', 'status');
  const dot = document.createElement('span');
  dot.className = 'audio-status__dot';
  dot.setAttribute('aria-hidden', 'true');
  const label = document.createElement('span');
  chip.append(dot, label);
  const stopBtn = document.getElementById('stopBtn');
  if (stopBtn?.parentElement === controls) stopBtn.after(chip);
  else controls.prepend(chip);

  const banner = document.createElement('div');
  banner.id = 'audioBanner';
  banner.className = 'audio-banner';
  banner.setAttribute('role', 'alert');
  banner.hidden = true;
  const text = document.createElement('span');
  const dismiss = document.createElement('button');
  dismiss.type = 'button';
  dismiss.className = 'audio-banner__dismiss';
  dismiss.textContent = '×';
  dismiss.setAttribute('aria-label', 'Dismiss audio warning');
  banner.append(text, dismiss);
  container.appendChild(banner);

  // A dismissed banner stays hidden until the problem changes.
  let dismissedState: string | null = null;
  let latest: AudioStatus | null = null;
  dismiss.addEventListener('click', () => {
    dismissedState = latest?.state ?? null;
    banner.hidden = true;
  });

  monitor.subscribe((status) => {
    latest = status;
    chip.classList.remove(...LEVEL_CLASSES);
    chip.classList.add(`audio-status--${status.level}`);
    chip.dataset.state = status.state;
    label.textContent = shortLabel(status);
    chip.title = describe(status, backendNote);

    // 'suspended' is only a warning after Play (the evaluator downgrades it to idle before).
    const showBanner = status.level === 'warn' || status.level === 'error';
    if (status.state !== dismissedState) dismissedState = null;
    text.textContent = status.message;
    banner.dataset.level = status.level;
    banner.hidden = !showBanner || dismissedState === status.state;
  });
}
