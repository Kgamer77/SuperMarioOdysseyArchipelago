import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { defineConfig } from 'vitest/config';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

export default defineConfig({
  root: __dirname,
  server: {
    fs: {
      allow: [__dirname, path.resolve(__dirname, '..')]
    }
  },
  test: {
    environment: 'node',
    include: ['src/**/*.test.ts']
  }
});
