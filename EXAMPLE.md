# Example: Adding a New WASM Function

This example shows how to add a new function to the AssemblyScript WASM module and use it in TypeScript.

## Step 1: Add Function to AssemblyScript

Edit `assembly/index.ts` and add your new function:

```typescript
/**
 * Square a number
 * @param x - Input number
 * @returns x squared
 */
export function square(x: i32): i32 {
  return x * x;
}
```

## Step 2: Rebuild WASM

```bash
npm run asbuild
```

This compiles your AssemblyScript code to WebAssembly.

## Step 3: Use in TypeScript

Edit `src/index.ts` and update the `WasmExports` interface:

```typescript
interface WasmExports {
  add: (a: number, b: number) => number;
  multiply: (a: number, b: number) => number;
  fibonacci: (n: number) => number;
  square: (x: number) => number;  // Add this line
  // ... other functions
}
```

Then use it in your code:

```typescript
const result = this.wasmModule.square(5);
console.log(`5 squared = ${result}`); // Output: 5 squared = 25
```

## Step 4: Rebuild the App

```bash
npm run build
# or for development with hot reload:
npm run dev
```

## Audio DSP Example

Here's a more complex example for audio processing:

### AssemblyScript (assembly/index.ts)

```typescript
/**
 * Simple delay effect
 * @param inputBuffer - Input audio buffer pointer
 * @param outputBuffer - Output audio buffer pointer
 * @param delayBuffer - Delay line buffer pointer
 * @param length - Buffer length
 * @param feedback - Feedback amount (0.0 to 1.0)
 */
export function processDelay(
  inputBuffer: usize,
  outputBuffer: usize,
  delayBuffer: usize,
  length: i32,
  feedback: f32
): void {
  for (let i = 0; i < length; i++) {
    const input = load<f32>(inputBuffer + (i << 2));
    const delayed = load<f32>(delayBuffer + (i << 2));
    
    const output = input + delayed * feedback;
    store<f32>(outputBuffer + (i << 2), output);
    store<f32>(delayBuffer + (i << 2), output);
  }
}
```

### TypeScript Usage (src/index.ts)

```typescript
// In your audio processing code:
const audioContext = new AudioContext();
const bufferSize = 1024;

// Create WASM memory buffers
const inputPtr = this.wasmModule.__new(bufferSize * 4, 1);
const outputPtr = this.wasmModule.__new(bufferSize * 4, 1);
const delayPtr = this.wasmModule.__new(bufferSize * 4, 1);

// Process audio
this.wasmModule.processDelay(inputPtr, outputPtr, delayPtr, bufferSize, 0.5);

// Clean up
this.wasmModule.__delete(inputPtr);
this.wasmModule.__delete(outputPtr);
this.wasmModule.__delete(delayPtr);
```

## Tips

1. **Performance**: WASM functions are much faster than JavaScript for DSP
2. **Memory**: Use WASM memory for large buffers
3. **Types**: i32 for integers, f32 for floats, f64 for doubles
4. **Debugging**: Use the debug build for better error messages

## Next Steps

- Explore the [AssemblyScript standard library](https://www.assemblyscript.org/stdlib/overview.html)
- Learn about [Web Audio API](https://developer.mozilla.org/en-US/docs/Web/API/Web_Audio_API)
- Check out [WASM memory management](https://www.assemblyscript.org/memory.html)
