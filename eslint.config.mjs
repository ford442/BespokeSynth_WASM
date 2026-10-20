// ESLint 9 flat config. Lints the TypeScript in src/, tests/ and playwright.config.ts with type
// information: src/ resolves through tsconfig.json, tests/ and playwright.config.ts through
// tsconfig.tests.json.
import js from '@eslint/js';
import globals from 'globals';
import tseslint from 'typescript-eslint';

export default tseslint.config(
  {
    ignores: [
      'dist/**',
      'node_modules/**',
      'wasm/**',
      'libs/**',
      'Source/**',
      'playwright-report/**',
      'test-results/**',
      // AudioWorklet processors are plain JS loaded via addModule; they were never part of the TS lint.
      'src/audio/processors/**',
      // Plain JS copied as-is (the opt-in COOP/COEP service worker).
      'src/coi/**',
    ],
  },
  js.configs.recommended,
  ...tseslint.configs.recommended,
  {
    files: ['**/*.ts', '**/*.tsx'],
    languageOptions: {
      ecmaVersion: 2022,
      sourceType: 'module',
      globals: {
        ...globals.browser,
        ...globals.node,
      },
      parserOptions: {
        project: ['./tsconfig.json', './tsconfig.tests.json'],
        tsconfigRootDir: import.meta.dirname,
      },
    },
    rules: {
      '@typescript-eslint/no-explicit-any': 'error',
      '@typescript-eslint/explicit-module-boundary-types': 'off',
      '@typescript-eslint/no-unnecessary-condition': 'error',
      '@typescript-eslint/no-floating-promises': 'error',
      'no-console': 'off',
    },
  },
);
