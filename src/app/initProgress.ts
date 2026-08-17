import { bespokeWindow } from './browserWindow';

export interface InitStep {
  id: string;
  label: string;
  weight: number;
}

export const WEBGPU_INIT_STEPS: InitStep[] = [
  { id: 'wasm_load', label: 'Loading WebAssembly module', weight: 10 },
  { id: 'webgpu_instance', label: 'Creating WebGPU instance', weight: 15 },
  { id: 'webgpu_surface', label: 'Creating WebGPU surface', weight: 10 },
  { id: 'webgpu_adapter', label: 'Requesting GPU adapter', weight: 15 },
  { id: 'webgpu_device', label: 'Acquiring GPU device', weight: 15 },
  { id: 'renderer_pipelines', label: 'Compiling shader pipelines', weight: 20 },
  { id: 'audio_init', label: 'Initializing audio backend', weight: 10 },
  { id: 'controls_create', label: 'Creating UI controls', weight: 5 },
];

export const WEBGL2_INIT_STEPS: InitStep[] = [
  { id: 'wasm_load', label: 'Loading WebAssembly module', weight: 10 },
  { id: 'webgl_context', label: 'Creating WebGL2 context', weight: 25 },
  { id: 'webgl_capabilities', label: 'Querying GL capabilities', weight: 15 },
  { id: 'renderer_pipelines', label: 'Compiling GLSL shader programs', weight: 25 },
  { id: 'audio_init', label: 'Initializing audio backend', weight: 15 },
  { id: 'controls_create', label: 'Creating UI controls', weight: 10 },
];

export class InitProgressController {
  private currentProgress = 0;
  private completedSteps = new Set<string>();
  private activeStep: string | null = null;
  private initStartTime = 0;
  private initSteps: InitStep[] = WEBGPU_INIT_STEPS;

  constructor(initSteps: InitStep[] = WEBGPU_INIT_STEPS) {
    this.initSteps = initSteps;
  }

  setInitSteps(steps: InitStep[]): void {
    this.initSteps = steps;
    this.completedSteps.clear();
    this.activeStep = null;
    this.currentProgress = 0;
  }

  markInitStart(): void {
    this.initStartTime = performance.now();
  }

  setupInitProgressCallback(): void {
    bespokeWindow.__bespoke_on_init_progress = (step: string, detail: string) => {
      console.log(`[InitProgress] ${step}: ${detail}`);

      const progressToStep: Record<string, string> = {
        init_start: 'wasm_load',
        webgpu_requested: 'webgpu_instance',
        webgpu_ready: 'webgpu_adapter',
        webgl_requested: 'webgl_context',
        webgl_ready: 'webgl_capabilities',
        renderer_init: 'renderer_pipelines',
        renderer_ready: 'renderer_pipelines',
        audio_init: 'audio_init',
        audio_ready: 'audio_init',
        controls_init: 'controls_create',
        init_complete: 'controls_create',
      };

      const mapped = progressToStep[step];
      if (mapped) {
        this.setActiveStep(mapped);
      }

      if (step.endsWith('_ready') || step === 'init_complete') {
        if (mapped) {
          this.completeStep(mapped);
        }
      }

      if (step.endsWith('_failed') || step === 'webgpu_start_failed') {
        const subheader = document.querySelector('#status .status-subheader');
        if (subheader) {
          subheader.textContent = detail;
        }
      }
    };
  }

  initializeProgressUI(): void {
    const stepsContainer = document.getElementById('init-steps');
    if (!stepsContainer) return;

    stepsContainer.innerHTML = '';
    this.initSteps.forEach((step) => {
      const stepEl = document.createElement('div');
      stepEl.className = 'init-step';
      stepEl.id = `step-${step.id}`;
      stepEl.innerHTML = `
        <span class="init-step-icon">○</span>
        <span class="init-step-text">${step.label}</span>
      `;
      stepsContainer.appendChild(stepEl);
    });
  }

  setActiveStep(stepId: string): void {
    this.activeStep = stepId;
    const stepEl = document.getElementById(`step-${stepId}`);
    if (stepEl) {
      stepEl.classList.add('active');
      stepEl.querySelector('.init-step-icon')!.textContent = '◌';
    }
    this.updateProgress();
    console.log(`[Init] Starting step: ${stepId}`);
  }

  completeStep(stepId: string): void {
    this.completedSteps.add(stepId);
    this.activeStep = null;

    const stepEl = document.getElementById(`step-${stepId}`);
    if (stepEl) {
      stepEl.classList.remove('active');
      stepEl.classList.add('completed');
      stepEl.querySelector('.init-step-icon')!.textContent = '✓';
    }

    this.updateProgress();
    const elapsed = ((performance.now() - this.initStartTime) / 1000).toFixed(2);
    console.log(`[Init] Completed step: ${stepId} (${elapsed}s)`);
  }

  completeAllSteps(): void {
    this.initSteps.forEach((step) => this.completeStep(step.id));
  }

  getInitSteps(): InitStep[] {
    return this.initSteps;
  }

  getActiveStep(): string | null {
    return this.activeStep;
  }

  getCompletedSteps(): Set<string> {
    return this.completedSteps;
  }

  getInitStartTime(): number {
    return this.initStartTime;
  }

  private updateProgress(): void {
    let progress = 0;

    this.initSteps.forEach((step) => {
      if (this.completedSteps.has(step.id)) {
        progress += step.weight;
      }
    });

    if (this.activeStep) {
      const activeStepData = this.initSteps.find((s) => s.id === this.activeStep);
      if (activeStepData) {
        progress += activeStepData.weight * 0.5;
      }
    }

    this.currentProgress = Math.min(100, Math.round(progress));

    const fillEl = document.getElementById('progress-fill');
    const textEl = document.getElementById('progress-text');

    if (fillEl) fillEl.style.width = `${this.currentProgress}%`;
    if (textEl) textEl.textContent = `${this.currentProgress}%`;
  }
}

export function showReadyState(rendererFallbackReason: string | null): void {
  const statusEl = document.getElementById('status');
  const subheaderEl = statusEl?.querySelector('.status-subheader');

  if (subheaderEl) {
    subheaderEl.textContent = 'Ready!';
  }

  updateFallbackBadge(rendererFallbackReason);

  setTimeout(() => {
    statusEl?.classList.add('hidden');
  }, 500);
}

export function updateFallbackBadge(rendererFallbackReason: string | null): void {
  const header = document.getElementById('header');
  if (!header) return;

  let badge = header.querySelector('.renderer-fallback-badge') as HTMLElement | null;
  if (rendererFallbackReason) {
    if (!badge) {
      badge = document.createElement('span');
      badge.className = 'renderer-fallback-badge';
      header.querySelector('h1')?.insertAdjacentElement('afterend', badge);
    }
    badge.textContent = 'WebGL2 fallback';
    badge.title = rendererFallbackReason;
  } else {
    badge?.remove();
  }
}

export function showErrorState(
  message: string,
  options: { canRetryWebGL?: boolean } = {},
): void {
  const statusEl = document.getElementById('status');
  const headerEl = statusEl?.querySelector('.status-header');
  const subheaderEl = statusEl?.querySelector('.status-subheader');

  if (statusEl) {
    statusEl.classList.remove('hidden');
    statusEl.classList.add('error');
  }
  if (headerEl) headerEl.textContent = 'Initialization Failed';
  if (subheaderEl) subheaderEl.textContent = message;

  const fillEl = document.getElementById('progress-fill');
  if (fillEl) {
    fillEl.style.width = '100%';
    fillEl.style.background = 'linear-gradient(90deg, #f44336, #ff5722)';
  }

  const textEl = document.getElementById('progress-text');
  if (textEl) {
    textEl.textContent = 'Error';
    textEl.style.color = '#f44336';
  }

  statusEl?.querySelector('.init-recovery')?.remove();
  const canRetryWebGL = options.canRetryWebGL ?? false;
  if (canRetryWebGL && statusEl) {
    const recovery = document.createElement('div');
    recovery.className = 'init-recovery';
    recovery.innerHTML = `
      <p class="init-recovery-hint">WebGPU is not available in this browser. The WebGL2 renderer works as a fallback.</p>
      <button type="button" class="btn init-retry-btn">Use WebGL2 renderer</button>
    `;
    recovery.querySelector('.init-retry-btn')?.addEventListener('click', () => {
      void import('../rendererMode').then(({ switchRendererPreference }) => {
        switchRendererPreference('webgl');
      });
    });
    statusEl.appendChild(recovery);
  }
}
