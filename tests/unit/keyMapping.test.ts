import { keyboardEventToWasmKeyCode } from '../../src/app/keyMapping.ts';
import assert from 'node:assert/strict';

function fakeKey(partial: Pick<KeyboardEvent, 'key'> & Partial<Pick<KeyboardEvent, 'code' | 'keyCode' | 'which'>>): KeyboardEvent {
  return partial as KeyboardEvent;
}

assert.equal(keyboardEventToWasmKeyCode(fakeKey({ key: 'a', code: 'KeyA' })), 65);
assert.equal(keyboardEventToWasmKeyCode(fakeKey({ key: 'A', code: 'KeyA' })), 65);
assert.equal(keyboardEventToWasmKeyCode(fakeKey({ key: 'Shift', code: 'ShiftLeft' })), 16);
assert.equal(keyboardEventToWasmKeyCode(fakeKey({ key: ' ', code: 'Space' })), 32);
assert.equal(keyboardEventToWasmKeyCode(fakeKey({ key: 'Tab', code: 'Tab' })), 9);
assert.equal(keyboardEventToWasmKeyCode(fakeKey({ key: 'Escape', code: 'Escape' })), 27);

console.log('keyMapping tests passed');
