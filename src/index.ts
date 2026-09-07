/**
 * BespokeSynth WASM Web Application Entry Point
 */

import './styles.css';
import { BespokeSynthApp } from './app/BespokeSynthApp';
import { installPublicApi } from './app/publicApi';

document.addEventListener('DOMContentLoaded', async () => {
  const app = new BespokeSynthApp();

  try {
    await app.init();
  } catch (error) {
    console.error('Application initialization failed:', error);
  }

  installPublicApi(app);

  window.addEventListener('beforeunload', () => {
    app.shutdown();
  });
});

export { BespokeSynthApp };
