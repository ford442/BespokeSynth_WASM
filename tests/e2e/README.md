# End-to-End Testing with Playwright

This directory contains Playwright tests for BespokeSynth WASM browser testing.

## Setup

Install dependencies (if not already done):
```bash
npm install
```

This will install `@playwright/test` and associated browsers.

## Running Tests

### Run all tests
```bash
npm run test:e2e
```

### Run tests with UI mode (interactive)
```bash
npm run test:e2e:ui
```

Useful for debugging and step-through testing. The UI mode shows test progress, allows pausing, and provides a visual debugger.

### Run tests in debug mode
```bash
npm run test:e2e:debug
```

Opens Playwright Inspector for step-by-step debugging.

### Run specific test file
```bash
npx playwright test tests/e2e/basic.spec.ts
```

### Run tests in specific browser
```bash
npx playwright test --project=chromium
npx playwright test --project=firefox
```

## Test Structure

Tests are organized by feature in individual `.spec.ts` files:

- `basic.spec.ts` - Initialization, canvas rendering, input handling, error detection

## Writing Tests

Tests use the standard Playwright API:

```typescript
import { test, expect } from '@playwright/test';

test('my test', async ({ page }) => {
  await page.goto('/');
  
  // Interact with WASM API
  const initState = await page.evaluate(() => {
    const Module = (window as any).Module;
    return Module.cwrap('bespoke_get_init_state', 'number', [])();
  });
  
  expect(initState).toBeGreaterThanOrEqual(5);
});
```

### Key Patterns for WASM Testing

**Wait for WASM module to load:**
```typescript
await page.waitForFunction(
  () => (window as any).Module?.cwrap !== undefined,
  { timeout: 10000 }
);
```

**Call WASM C API functions:**
```typescript
const result = await page.evaluate(() => {
  const Module = (window as any).Module;
  return Module.cwrap('function_name', 'return_type', ['arg_types'])(...args);
});
```

**Get initialization state:**
```typescript
const state = await page.evaluate(() => {
  const Module = (window as any).Module;
  return Module.cwrap('bespoke_get_init_state', 'number', [])();
});
// Expected: 5 = FullyInitialized
```

**Check canvas rendering:**
```typescript
const canvas = page.locator('canvas');
await expect(canvas).toBeVisible();
const boundingBox = await canvas.boundingBox();
```

**Simulate input:**
```typescript
await page.mouse.move(x, y);
await page.mouse.down();
await page.mouse.up();
await page.keyboard.press('Space');
```

## Configuration

See `playwright.config.ts` at the repository root:
- **Base URL**: http://localhost:8080
- **Browsers**: Chromium, Firefox, WebKit
- **Screenshots**: On failure only
- **Traces**: On first retry
- **Web server**: Auto-starts dev server if not running

## Reports

After running tests, view the HTML report:
```bash
npx playwright show-report
```

This opens an interactive report showing:
- Test results and timings
- Screenshots and videos
- Trace files for debugging

## CI/CD Integration

Tests automatically run in CI with:
- 2 retry attempts on failure
- Single worker (safer for shared resources)
- Full trace recording

To run tests locally in CI mode:
```bash
CI=true npm run test:e2e
```

## Troubleshooting

### Tests hang waiting for initialization
- Verify the dev server is running: `npm run dev`
- Check browser console for errors
- Increase timeout: `{ timeout: 15000 }`

### Canvas rendering not visible
- Ensure WebGPU is enabled in the browser
- Check for shader compilation errors in console
- Verify WebGPU context initialization succeeded

### WASM function calls fail
- Confirm `Module.cwrap` is available
- Use correct return type ('number', 'string', etc.)
- Check C API function is wrapped with `EMSCRIPTEN_KEEPALIVE`

## Resources

- [Playwright Documentation](https://playwright.dev)
- [WASM Testing Patterns](https://playwright.dev/docs/api/class-page#page-evaluate)
- [BespokeSynth C API](../wasm/src/WasmBridge.cpp)
