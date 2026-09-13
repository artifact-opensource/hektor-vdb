// Electron main process entry point
// The 'electron' module should resolve to Electron's internal bindings when running in Electron
const electron = require('electron');

// Handle the case where electron module returns just the binary path (shouldn't happen in Electron process)
if (typeof electron === 'string') {
  console.error('ERROR: Electron module returned a path string instead of the API.');
  console.error('This usually means the app is not running in the Electron process.');
  console.error('Make sure to run this with: npm start');
  process.exit(1);
}

const { app, BrowserWindow, ipcMain } = electron;
import * as path from 'path';
import * as os from 'os';
import * as fs from 'fs';

let mainWindow: typeof BrowserWindow.prototype | null = null;

function resolveDatabasePath(): string {
  const configuredPath = process.env.HEKTOR_DB_PATH || process.env.VDB_PATH || './data/vectors';
  return path.isAbsolute(configuredPath)
    ? configuredPath
    : path.resolve(process.cwd(), configuredPath);
}

function getApiBaseUrl(): string {
  return process.env.HEKTOR_API_URL || process.env.VDB_API_URL || 'http://127.0.0.1:8080';
}

function safeFileSize(filePath: string): number {
  try {
    return fs.statSync(filePath).size;
  } catch {
    return 0;
  }
}

function readCollectionsRegistry(dbPath: string) {
  const registryPath = path.join(dbPath, 'collections.json');
  if (!fs.existsSync(registryPath)) {
    return [];
  }

  try {
    const parsed = JSON.parse(fs.readFileSync(registryPath, 'utf-8'));
    if (!Array.isArray(parsed)) {
      return [];
    }

    return parsed
      .filter((item) => item && typeof item === 'object')
      .map((item: any) => ({
        name: String(item.name ?? ''),
        dimension: Number(item.dimension ?? 0),
        metric: String(item.metric ?? 'cosine'),
        documentCount: Number(item.document_count ?? 0),
        createdAt: item.created_at ? String(item.created_at) : null,
      }))
      .filter((item) => item.name);
  } catch (error) {
    console.warn('Failed to read collections registry:', error);
    return [];
  }
}

function createWindow() {
  mainWindow = new BrowserWindow({
    width: 1920,
    height: 1080,
    minWidth: 1280,
    minHeight: 720,
    frame: true,
    titleBarStyle: 'hiddenInset',
    backgroundColor: '#0f172a',
    webPreferences: {
      preload: path.join(__dirname, '../preload/index.js'),
      contextIsolation: true,
      nodeIntegration: false,
      sandbox: false
    }
  });

  // Load the app
  if (process.env.NODE_ENV === 'development') {
    mainWindow.loadURL('http://localhost:5173');
    mainWindow.webContents.openDevTools();
  } else {
    mainWindow.loadFile(path.join(__dirname, '../renderer/index.html'));
  }

  mainWindow.on('closed', () => {
    mainWindow = null;
  });
}

app.whenReady().then(() => {
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

// IPC Handlers
ipcMain.handle('get-app-version', () => {
  return app.getVersion();
});

ipcMain.handle('get-theme', async (_event: any, themeName: string) => {
  // Theme loading logic will be implemented
  return { name: themeName, loaded: true };
});

ipcMain.handle('get-studio-config', () => {
  return {
    apiBaseUrl: getApiBaseUrl(),
    dbPath: resolveDatabasePath(),
  };
});

ipcMain.handle('get-system-overview', async () => {
  const totalMemoryBytes = os.totalmem();
  const freeMemoryBytes = os.freemem();

  return {
    hostname: os.hostname(),
    platform: process.platform,
    arch: process.arch,
    cpuModel: os.cpus()[0]?.model ?? 'Unknown CPU',
    cpuCores: os.cpus().length,
    loadAverage: os.loadavg(),
    totalMemoryBytes,
    freeMemoryBytes,
    usedMemoryBytes: totalMemoryBytes - freeMemoryBytes,
    uptimeSeconds: os.uptime(),
    appMemoryBytes: process.memoryUsage().rss,
    nodeVersion: process.version,
  };
});

ipcMain.handle('get-local-database-overview', async () => {
  const dbPath = resolveDatabasePath();
  const collections = readCollectionsRegistry(dbPath);

  return {
    dbPath,
    collections,
    collectionCount: collections.length,
    trackedDocuments: collections.reduce((sum, collection) => sum + collection.documentCount, 0),
    vectorsFileBytes: safeFileSize(path.join(dbPath, 'vectors.bin')),
    indexFileBytes: safeFileSize(path.join(dbPath, 'index.hnsw')),
    metadataFileBytes: safeFileSize(path.join(dbPath, 'metadata.jsonl')),
    configFileBytes: safeFileSize(path.join(dbPath, 'config.json')),
    registryPresent: fs.existsSync(path.join(dbPath, 'collections.json')),
  };
});
