import assert from 'node:assert/strict';
import { QwertyPiano, qwertyPianoOffset } from '../../src/app/qwertyPiano.ts';
import { isEditableTarget } from '../../src/app/keyMapping.ts';

type Event = { code: string; repeat?: boolean; shiftKey?: boolean; ctrlKey?: boolean; metaKey?: boolean; altKey?: boolean };
const key = (e: Event) => ({ repeat: false, shiftKey: false, ctrlKey: false, metaKey: false, altKey: false, ...e });

const log: string[] = [];
const piano = new QwertyPiano({
  noteOn: (channel, pitch, velocity) => log.push(`on ${channel} ${pitch} ${velocity}`),
  noteOff: (channel, pitch) => log.push(`off ${channel} ${pitch}`),
});

assert.equal(qwertyPianoOffset('KeyZ'), 0);
assert.equal(qwertyPianoOffset('KeyQ'), 12);
assert.equal(qwertyPianoOffset('Slash'), undefined, '"/" stays the spawn-menu shortcut');
assert.equal(qwertyPianoOffset('KeyA'), undefined);

assert.equal(piano.handleKeyDown(key({ code: 'KeyZ' })), true);
assert.deepEqual(log, ['on 0 60 0.8'], 'Z is C4 at the default velocity');
assert.equal(piano.handleKeyDown(key({ code: 'KeyZ', repeat: true })), true);
assert.equal(log.length, 1, 'auto-repeat does not retrigger');
assert.equal(piano.handleKeyUp({ code: 'KeyZ' }), true);
assert.equal(log[1], 'off 0 60');

piano.handleKeyDown(key({ code: 'KeyE', shiftKey: true }));
assert.equal(log[2], 'on 0 76 1', 'shift accents to full velocity; E is E5');

piano.handleKeyDown(key({ code: 'Equal' }));
assert.equal(piano.octave, 5);
piano.handleKeyUp({ code: 'KeyE' });
assert.equal(log[3], 'off 0 76', 'note-off uses the pitch captured at note-on');

assert.equal(piano.handleKeyDown(key({ code: 'KeyZ', ctrlKey: true })), false, 'Ctrl+Z is undo, not a note');
assert.equal(piano.handleKeyDown(key({ code: 'KeyA' })), false);

piano.handleKeyDown(key({ code: 'BracketLeft' }));
assert.equal(piano.velocity, 0.7);

piano.handleKeyDown(key({ code: 'KeyX' }));
piano.handleKeyDown(key({ code: 'KeyC' }));
piano.setEnabled(false);
assert.deepEqual(log.slice(-2), ['off 0 74', 'off 0 76'], 'disabling releases held notes');
assert.equal(piano.handleKeyDown(key({ code: 'KeyZ' })), false, 'disabled piano forwards keys');

assert.equal(isEditableTarget({ tagName: 'TEXTAREA' } as unknown as EventTarget), true);
assert.equal(isEditableTarget({ tagName: 'CANVAS' } as unknown as EventTarget), false);
assert.equal(isEditableTarget(null), false);

console.log('qwertyPiano tests passed');
