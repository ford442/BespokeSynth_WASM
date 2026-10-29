// Brings the global `Window.__bespoke` augmentation declared in src/app/publicApi.ts into scope for
// the specs, so page.evaluate callbacks can use `window.__bespoke` with the app's real API type.
import type {} from '../../src/app/publicApi';
