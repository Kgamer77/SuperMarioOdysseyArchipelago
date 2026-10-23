import vue from '@vitejs/plugin-vue';
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import type { IncomingMessage, ServerResponse } from 'node:http';
import type { Plugin } from 'vite';
import { defineConfig } from 'vite';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const logicDir = path.resolve(__dirname, '..');
const repositoryDir = path.resolve(logicDir, '..');
const savesDir = path.resolve(__dirname, 'saves');
const kingdomSavesDir = path.resolve(savesDir, 'kingdoms');

async function readBody(req: IncomingMessage): Promise<string> {
  const chunks: Buffer[] = [];
  for await (const chunk of req) {
    chunks.push(Buffer.isBuffer(chunk) ? chunk : Buffer.from(chunk));
  }

  return Buffer.concat(chunks).toString('utf-8');
}

function sendJson(res: ServerResponse, statusCode: number, body: unknown): void {
  res.statusCode = statusCode;
  res.setHeader('Content-Type', 'application/json; charset=utf-8');
  res.end(JSON.stringify(body));
}

function slugifyKingdom(kingdom: string): string {
  const slug = kingdom
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '_')
    .replace(/^_+|_+$/g, '');
  return slug || 'unknown';
}

function kingdomProjectPath(kingdom: string): string {
  return path.resolve(kingdomSavesDir, `${slugifyKingdom(kingdom)}.json`);
}

function relativeEditorPath(filePath: string): string {
  return path.relative(__dirname, filePath).replace(/\\/g, '/');
}

function queryParam(req: IncomingMessage, name: string): string {
  const url = new URL(req.url ?? '', 'http://logic-editor.local');
  return url.searchParams.get(name)?.trim() ?? '';
}

function logicEditorSavePlugin(): Plugin {
  return {
    name: 'logic-editor-save-api',
    configureServer(server) {
      server.middlewares.use('/api/logic-editor/load', async (req, res, next) => {
        if (req.method !== 'GET') {
          next();
          return;
        }

        try {
          const kingdom = queryParam(req, 'kingdom');
          if (!kingdom) {
            sendJson(res, 400, { ok: false, error: 'Missing kingdom query parameter.' });
            return;
          }

          const projectPath = kingdomProjectPath(kingdom);
          const text = await fs.readFile(projectPath, 'utf-8');
          sendJson(res, 200, { ok: true, project: JSON.parse(text) });
        } catch (error) {
          const code = (error as NodeJS.ErrnoException).code;
          if (code === 'ENOENT') {
            sendJson(res, 404, { ok: false, error: 'No saved layout exists for this kingdom yet.' });
            return;
          }

          sendJson(res, 500, { ok: false, error: String(error) });
        }
      });

      server.middlewares.use('/api/logic-editor/save', async (req, res, next) => {
        if (req.method !== 'POST') {
          next();
          return;
        }

        try {
          const body = JSON.parse(await readBody(req)) as {
            kingdom?: string;
            project: unknown;
          };
          const kingdom = body.kingdom?.trim();
          if (!kingdom) {
            sendJson(res, 400, { ok: false, error: 'Missing kingdom in save request.' });
            return;
          }

          const projectPath = kingdomProjectPath(kingdom);
          await fs.mkdir(kingdomSavesDir, { recursive: true });
          await fs.writeFile(projectPath, JSON.stringify(body.project, null, 2), 'utf-8');

          sendJson(res, 200, {
            ok: true,
            projectPath: relativeEditorPath(projectPath)
          });
        } catch (error) {
          sendJson(res, 500, { ok: false, error: String(error) });
        }
      });
    }
  };
}

export default defineConfig({
  root: __dirname,
  plugins: [vue(), logicEditorSavePlugin()],
  server: {
    port: 5174,
    strictPort: false,
    fs: {
      allow: [__dirname, logicDir, repositoryDir]
    }
  },
  preview: {
    port: 4174,
    strictPort: false
  },
  build: {
    outDir: 'dist',
    emptyOutDir: true
  }
});
