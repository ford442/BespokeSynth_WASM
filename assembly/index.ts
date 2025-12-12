/**
 * BespokeSynth WASM - AssemblyScript Module
 * 
 * This module contains the core WASM functions written in AssemblyScript.
 * AssemblyScript is a TypeScript-like language that compiles to WebAssembly.
 * 
 * This is a basic example showing math operations. The actual synth
 * functionality (audio processing, DSP, etc.) would be implemented here.
 */

/**
 * Add two numbers
 * @param a - First number
 * @param b - Second number
 * @returns The sum of a and b
 */
export function add(a: i32, b: i32): i32 {
  return a + b;
}

/**
 * Multiply two numbers
 * @param a - First number
 * @param b - Second number
 * @returns The product of a and b
 */
export function multiply(a: i32, b: i32): i32 {
  return a * b;
}

/**
 * Calculate Fibonacci number (recursive implementation)
 * @param n - The position in the Fibonacci sequence
 * @returns The nth Fibonacci number
 */
export function fibonacci(n: i32): i32 {
  if (n <= 1) return n;
  return fibonacci(n - 1) + fibonacci(n - 2);
}

/**
 * Simple oscillator function - generates a sine wave value
 * This is a basic example of audio DSP functionality
 * @param phase - Current phase (0.0 to 1.0)
 * @returns Sine wave value (-1.0 to 1.0)
 */
export function sineOscillator(phase: f32): f32 {
  return Mathf.sin(phase * 2.0 * Mathf.PI);
}

/**
 * Calculate RMS (Root Mean Square) of an audio buffer
 * This is useful for audio level detection
 * @param bufferPtr - Pointer to the audio buffer
 * @param length - Length of the buffer
 * @returns RMS value
 */
export function calculateRMS(bufferPtr: usize, length: i32): f32 {
  let sum: f32 = 0.0;
  
  for (let i = 0; i < length; i++) {
    const value = load<f32>(bufferPtr + (i << 2)); // Load f32 at offset
    sum += value * value;
  }
  
  return Mathf.sqrt(sum / f32(length));
}

/**
 * Simple low-pass filter coefficient calculation
 * Used for audio filtering
 * @param cutoff - Cutoff frequency (0.0 to 1.0, normalized to sample rate)
 * @returns Filter coefficient
 */
export function lowPassCoefficient(cutoff: f32): f32 {
  return 1.0 - Mathf.exp(-2.0 * Mathf.PI * cutoff);
}

/**
 * Apply gain to a value with smooth interpolation
 * @param input - Input value
 * @param targetGain - Target gain value
 * @param currentGain - Current gain value
 * @param smoothing - Smoothing factor (0.0 to 1.0)
 * @returns Processed output value
 */
export function applyGain(input: f32, targetGain: f32, currentGain: f32, smoothing: f32): f32 {
  const gain = currentGain + (targetGain - currentGain) * smoothing;
  return input * gain;
}
