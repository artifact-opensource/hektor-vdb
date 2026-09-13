import React, { useCallback, useEffect, useMemo, useState } from 'react';
import { useTheme } from './lib/theme-system';
import VectorSpace3D from './components/3d/VectorSpace3D';
import { PerceptualQuantizationPanel } from './components/quantization';

type ViewMode = 'dashboard' | 'search' | 'ingest' | 'analytics' | '3d' | 'quantization';

interface DemoVector {
  id: string;
  position: [number, number, number];
  color: string;
  distance: number;
}

interface LocalCollection {
  name: string;
  dimension: number;
  metric: string;
  documentCount: number;
  createdAt: string | null;
}

interface LocalDatabaseOverview {
  dbPath: string;
  collections: LocalCollection[];
  collectionCount: number;
  trackedDocuments: number;
  vectorsFileBytes: number;
  indexFileBytes: number;
  metadataFileBytes: number;
  configFileBytes: number;
  registryPresent: boolean;
}

interface SystemOverview {
  hostname: string;
  platform: string;
  arch: string;
  cpuModel: string;
  cpuCores: number;
  loadAverage: number[];
  totalMemoryBytes: number;
  freeMemoryBytes: number;
  usedMemoryBytes: number;
  uptimeSeconds: number;
  appMemoryBytes: number;
  nodeVersion: string;
}

interface StudioConfig {
  apiBaseUrl: string;
  dbPath: string;
}

interface HealthResponse {
  status: string;
  version: string;
  database: string;
  uptime_seconds: number;
}

interface StatsResponse {
  total_vectors: number;
  memory_usage_bytes: number;
  index_size: number;
  collections: number;
}

interface CollectionInfo {
  name: string;
  dimension: number;
  metric: string;
  document_count: number;
  created_at?: string | null;
}

interface SearchResultItem {
  id: string;
  score: number;
  content?: string | null;
  metadata: Record<string, unknown>;
}

interface CollectionDraft {
  name: string;
  dimension: number;
  metric: string;
}

function formatBytes(bytes: number): string {
  if (!bytes) return '0 B';
  const units = ['B', 'KB', 'MB', 'GB', 'TB'];
  let value = bytes;
  let unitIndex = 0;
  while (value >= 1024 && unitIndex < units.length - 1) {
    value /= 1024;
    unitIndex += 1;
  }
  return `${value.toFixed(value >= 10 || unitIndex === 0 ? 0 : 1)} ${units[unitIndex]}`;
}

function formatUptime(seconds: number): string {
  const total = Math.max(0, Math.floor(seconds));
  const days = Math.floor(total / 86400);
  const hours = Math.floor((total % 86400) / 3600);
  const minutes = Math.floor((total % 3600) / 60);
  if (days > 0) return `${days}d ${hours}h`;
  if (hours > 0) return `${hours}h ${minutes}m`;
  return `${minutes}m`;
}

function formatNativeStatusVersion(version: unknown): string {
  if (typeof version === 'string') return version;
  if (version && typeof version === 'object' && 'version' in version) {
    return String((version as { version: unknown }).version);
  }
  return 'unknown';
}

function buildDemoVectors(count: number): DemoVector[] {
  return Array.from({ length: count }, (_, index) => ({
    id: `vec_${index}`,
    position: [
      Math.sin(index * 0.47) * 2.5,
      Math.cos(index * 0.29) * 2.2,
      Math.sin(index * 0.13) * Math.cos(index * 0.41) * 2.8,
    ],
    color: `hsl(${(index * 137.5) % 360}, 70%, 60%)`,
    distance: (index % 10) / 10,
  }));
}

function App() {
  const { currentTheme, setTheme, availableThemes } = useTheme();
  const [viewMode, setViewMode] = useState<ViewMode>('dashboard');
  const [nativeStatus, setNativeStatus] = useState('checking...');
  const [demoVectors, setDemoVectors] = useState<DemoVector[]>(() => buildDemoVectors(120));
  const [config, setConfig] = useState<StudioConfig>({
    apiBaseUrl: 'http://127.0.0.1:8080',
    dbPath: '',
  });
  const [systemOverview, setSystemOverview] = useState<SystemOverview | null>(null);
  const [localDbOverview, setLocalDbOverview] = useState<LocalDatabaseOverview | null>(null);
  const [apiHealth, setApiHealth] = useState<HealthResponse | null>(null);
  const [apiStats, setApiStats] = useState<StatsResponse | null>(null);
  const [collections, setCollections] = useState<CollectionInfo[]>([]);
  const [overviewError, setOverviewError] = useState<string | null>(null);
  const [apiUrl, setApiUrl] = useState('http://127.0.0.1:8080');
  const [apiUsername, setApiUsername] = useState('');
  const [apiPassword, setApiPassword] = useState('');
  const [authToken, setAuthToken] = useState<string | null>(null);
  const [apiStatusMessage, setApiStatusMessage] = useState('Checking local API...');
  const [selectedCollection, setSelectedCollection] = useState('');
  const [searchQuery, setSearchQuery] = useState('');
  const [searchTopK, setSearchTopK] = useState(10);
  const [searchFiltersText, setSearchFiltersText] = useState('');
  const [searchResults, setSearchResults] = useState<SearchResultItem[]>([]);
  const [searchError, setSearchError] = useState<string | null>(null);
  const [managementMessage, setManagementMessage] = useState<string | null>(null);
  const [managementError, setManagementError] = useState<string | null>(null);
  const [isConnecting, setIsConnecting] = useState(false);
  const [isSearching, setIsSearching] = useState(false);
  const [isCreatingCollection, setIsCreatingCollection] = useState(false);
  const [isDeletingCollection, setIsDeletingCollection] = useState(false);
  const [isAddingDocument, setIsAddingDocument] = useState(false);
  const [collectionDraft, setCollectionDraft] = useState<CollectionDraft>({
    name: '',
    dimension: 1536,
    metric: 'cosine',
  });
  const [documentContent, setDocumentContent] = useState('');
  const [documentType, setDocumentType] = useState('general');
  const [documentMetadataText, setDocumentMetadataText] = useState('');

  const visibleCollections = useMemo(() => {
    if (collections.length > 0) {
      return collections.map((collection) => ({
        name: collection.name,
        dimension: collection.dimension,
        metric: collection.metric,
        documentCount: collection.document_count,
        createdAt: collection.created_at ?? null,
      }));
    }

    return localDbOverview?.collections ?? [];
  }, [collections, localDbOverview]);

  const refreshLocalOverview = useCallback(async () => {
    if (!window.electronAPI) return;

    try {
      const [nextConfig, system, localDb] = await Promise.all([
        window.electronAPI.getStudioConfig(),
        window.electronAPI.getSystemOverview(),
        window.electronAPI.getLocalDatabaseOverview(),
      ]);

      setConfig(nextConfig);
      setApiUrl(nextConfig.apiBaseUrl);
      setSystemOverview(system);
      setLocalDbOverview(localDb);
      setDemoVectors(buildDemoVectors(Math.max(48, Math.min(localDb.trackedDocuments || 120, 240))));
      setOverviewError(null);
    } catch (error) {
      setOverviewError(error instanceof Error ? error.message : 'Failed to load local overview');
    }
  }, []);

  const refreshApiHealth = useCallback(async (baseUrl: string) => {
    try {
      const response = await fetch(`${baseUrl}/health`);
      if (!response.ok) {
        throw new Error(`Health check failed with status ${response.status}`);
      }
      const health = await response.json() as HealthResponse;
      setApiHealth(health);
      setApiStatusMessage(`Connected to API ${health.version} (${health.database})`);
    } catch (error) {
      setApiHealth(null);
      setApiStats(null);
      if (!authToken) {
        setCollections([]);
      }
      setApiStatusMessage(error instanceof Error ? error.message : 'API unavailable');
    }
  }, [authToken]);

  const refreshAuthorizedOverview = useCallback(async (baseUrl: string, token: string) => {
    const authHeader = 'Bearer '.concat(token);
    const headers = {
      Authorization: authHeader,
    };

    const [statsResponse, collectionsResponse] = await Promise.all([
      fetch(`${baseUrl}/stats`, { headers }),
      fetch(`${baseUrl}/collections`, { headers }),
    ]);

    if (!statsResponse.ok) {
      throw new Error(`Stats request failed with status ${statsResponse.status}`);
    }
    if (!collectionsResponse.ok) {
      throw new Error(`Collections request failed with status ${collectionsResponse.status}`);
    }

    const nextStats = await statsResponse.json() as StatsResponse;
    const nextCollections = await collectionsResponse.json() as CollectionInfo[];
    setApiStats(nextStats);
    setCollections(nextCollections);
    setSelectedCollection((current) => current || nextCollections[0]?.name || '');
  }, []);

  const refreshAuthedData = useCallback(async (baseUrl?: string, token?: string) => {
    const nextBaseUrl = baseUrl ?? apiUrl;
    const nextToken = token ?? authToken;
    await refreshLocalOverview();
    await refreshApiHealth(nextBaseUrl);
    if (nextToken) {
      await refreshAuthorizedOverview(nextBaseUrl, nextToken);
    }
  }, [apiUrl, authToken, refreshApiHealth, refreshAuthorizedOverview, refreshLocalOverview]);

  const refreshOverview = useCallback(async () => {
    try {
      await refreshAuthedData();
    } catch (error) {
      setApiStats(null);
      setCollections([]);
      setApiStatusMessage(error instanceof Error ? error.message : 'Authenticated requests failed');
    }
  }, [refreshAuthedData]);

  useEffect(() => {
    if (currentTheme.name === 'quantum-slate-dark') {
      const midnightPurple = availableThemes.find((theme) => theme.name === 'midnight-purple-dark');
      if (midnightPurple) {
        setTheme(midnightPurple.name);
      }
    }
  }, [availableThemes, currentTheme.name, setTheme]);

  useEffect(() => {
    if (window.hektorAPI) {
      const isAvailable = window.hektorAPI.isNativeAvailable();
      if (isAvailable) {
        const version = formatNativeStatusVersion(window.hektorAPI.getVersion());
        setNativeStatus(`✅ Native C++ addon active (v${version})`);
      } else {
        setNativeStatus('⚠️ Native addon not compiled');
      }
    } else {
      setNativeStatus('⚠️ Native addon bridge unavailable');
    }
  }, []);

  useEffect(() => {
    void refreshOverview();
    const intervalId = window.setInterval(() => {
      void refreshOverview();
    }, 5000);

    return () => window.clearInterval(intervalId);
  }, [refreshOverview]);

  const handleConnect = useCallback(async () => {
    setIsConnecting(true);
    setSearchError(null);

    try {
      const response = await fetch(`${apiUrl}/auth/login`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          username: apiUsername,
          password: apiPassword,
        }),
      });

      if (!response.ok) {
        throw new Error(`Login failed with status ${response.status}`);
      }

      const payload = await response.json() as { access_token: string };
      setAuthToken(payload.access_token);
      setApiStatusMessage('Authenticated to local API');
      await refreshAuthedData(apiUrl, payload.access_token);
    } catch (error) {
      setAuthToken(null);
      setApiStats(null);
      setCollections([]);
      setSearchError(error instanceof Error ? error.message : 'Unable to authenticate');
    } finally {
      setIsConnecting(false);
    }
  }, [apiPassword, apiUrl, apiUsername, refreshAuthedData]);

  const createAuthHeaders = useCallback(() => {
    if (!authToken) {
      throw new Error('Connect to the API first.');
    }

    return {
      'Content-Type': 'application/json',
      Authorization: 'Bearer '.concat(authToken),
    };
  }, [authToken]);

  const handleCreateCollection = useCallback(async () => {
    setManagementError(null);
    setManagementMessage(null);

    if (!collectionDraft.name.trim()) {
      setManagementError('Collection name is required.');
      return;
    }

    setIsCreatingCollection(true);
    try {
      const response = await fetch(`${apiUrl}/collections`, {
        method: 'POST',
        headers: createAuthHeaders(),
        body: JSON.stringify({
          name: collectionDraft.name.trim(),
          dimension: Number(collectionDraft.dimension),
          metric: collectionDraft.metric,
        }),
      });

      if (!response.ok) {
        throw new Error(`Create collection failed with status ${response.status}`);
      }

      const created = await response.json() as CollectionInfo;
      setCollectionDraft({ name: '', dimension: created.dimension, metric: created.metric });
      setSelectedCollection(created.name);
      setManagementMessage(`Created collection ${created.name}.`);
      await refreshAuthedData();
    } catch (error) {
      setManagementError(error instanceof Error ? error.message : 'Failed to create collection');
    } finally {
      setIsCreatingCollection(false);
    }
  }, [apiUrl, collectionDraft, createAuthHeaders, refreshAuthedData]);

  const handleDeleteCollection = useCallback(async (collectionName: string) => {
    setManagementError(null);
    setManagementMessage(null);

    if (!window.confirm(`Delete collection "${collectionName}"?`)) {
      return;
    }

    setIsDeletingCollection(true);
    try {
      const response = await fetch(`${apiUrl}/collections/${encodeURIComponent(collectionName)}`, {
        method: 'DELETE',
        headers: createAuthHeaders(),
      });

      if (!response.ok) {
        throw new Error(`Delete collection failed with status ${response.status}`);
      }

      setSelectedCollection((current) => (current === collectionName ? '' : current));
      setManagementMessage(`Deleted collection ${collectionName}.`);
      await refreshAuthedData();
    } catch (error) {
      setManagementError(error instanceof Error ? error.message : 'Failed to delete collection');
    } finally {
      setIsDeletingCollection(false);
    }
  }, [apiUrl, createAuthHeaders, refreshAuthedData]);

  const handleAddDocument = useCallback(async () => {
    setManagementError(null);
    setManagementMessage(null);

    if (!selectedCollection) {
      setManagementError('Select a collection before ingesting.');
      return;
    }

    if (!documentContent.trim()) {
      setManagementError('Document content is required.');
      return;
    }

    let parsedMetadata: Record<string, unknown> = {};
    if (documentMetadataText.trim()) {
      try {
        parsedMetadata = JSON.parse(documentMetadataText) as Record<string, unknown>;
      } catch {
        setManagementError('Document metadata must be valid JSON.');
        return;
      }
    }

    setIsAddingDocument(true);
    try {
      const response = await fetch(`${apiUrl}/collections/${encodeURIComponent(selectedCollection)}/documents`, {
        method: 'POST',
        headers: createAuthHeaders(),
        body: JSON.stringify({
          content: documentContent,
          metadata: parsedMetadata,
          document_type: documentType || 'general',
        }),
      });

      if (!response.ok) {
        throw new Error(`Add document failed with status ${response.status}`);
      }

      const payload = await response.json() as { id: string };
      setDocumentContent('');
      setDocumentMetadataText('');
      setManagementMessage(`Added document ${payload.id} to ${selectedCollection}.`);
      await refreshAuthedData();
    } catch (error) {
      setManagementError(error instanceof Error ? error.message : 'Failed to add document');
    } finally {
      setIsAddingDocument(false);
    }
  }, [apiUrl, createAuthHeaders, documentContent, documentMetadataText, documentType, refreshAuthedData, selectedCollection]);

  const handleSearch = useCallback(async () => {
    setSearchError(null);
    setSearchResults([]);

    if (!authToken) {
      setSearchError('Connect to the API to run search queries.');
      return;
    }

    if (!selectedCollection) {
      setSearchError('Select a collection first.');
      return;
    }

    let parsedFilters: Record<string, unknown> | undefined;
    if (searchFiltersText.trim()) {
      try {
        parsedFilters = JSON.parse(searchFiltersText) as Record<string, unknown>;
      } catch {
        setSearchError('Filters must be valid JSON.');
        return;
      }
    }

    setIsSearching(true);
    try {
      const authHeader = 'Bearer '.concat(authToken);
      const response = await fetch(`${apiUrl}/collections/${encodeURIComponent(selectedCollection)}/search`, {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          Authorization: authHeader,
        },
        body: JSON.stringify({
          query: searchQuery,
          k: searchTopK,
          filters: parsedFilters,
        }),
      });

      if (!response.ok) {
        throw new Error(`Search failed with status ${response.status}`);
      }

      const payload = await response.json() as SearchResultItem[];
      setSearchResults(payload);
    } catch (error) {
      setSearchError(error instanceof Error ? error.message : 'Search failed');
    } finally {
      setIsSearching(false);
    }
  }, [apiUrl, authToken, searchFiltersText, searchQuery, searchTopK, selectedCollection]);

  const dashboardCards = [
    {
      label: 'Collections',
      value: (apiStats?.collections ?? localDbOverview?.collectionCount ?? 0).toLocaleString(),
      detail: 'Tracked datasets',
    },
    {
      label: 'Tracked Documents',
      value: (localDbOverview?.trackedDocuments ?? 0).toLocaleString(),
      detail: 'Persisted in local registry',
    },
    {
      label: 'Vectors',
      value: (apiStats?.total_vectors ?? 0).toLocaleString(),
      detail: 'Reported by API/native stats',
    },
    {
      label: 'Index Size',
      value: formatBytes(apiStats?.index_size ?? localDbOverview?.indexFileBytes ?? 0),
      detail: 'Active HNSW footprint',
    },
  ];

  const renderBackButton = () => (
    <button
      onClick={() => setViewMode('dashboard')}
      className="demo-button"
      style={{ marginBottom: '15px', background: 'transparent', border: '1px solid var(--border)' }}
    >
      ← Back to Dashboard
    </button>
  );

  return (
    <div className="app-container" data-theme={currentTheme.name}>
      <header className="app-header">
        <div>
          <h1 className="app-title">HEKTOR Studio</h1>
          <p style={{ fontSize: '11px', opacity: 0.7, marginTop: '2px' }}>
            Local-first machine-memory command center
          </p>
        </div>
        <div className="theme-selector">
          <span style={{ fontSize: '13px', marginRight: '10px', opacity: 0.8 }}>
            {nativeStatus}
          </span>
          <select
            value={currentTheme.name}
            onChange={(event) => setTheme(event.target.value)}
            className="theme-dropdown"
          >
            {availableThemes.map((theme) => (
              <option key={theme.name} value={theme.name}>
                {theme.displayName}
              </option>
            ))}
          </select>
        </div>
      </header>

      <main className="app-main">
        {viewMode === 'dashboard' ? (
          <div className="welcome-section">
            <div className="status-badge">
              API: {apiStatusMessage} · DB Path: {config.dbPath || 'unconfigured'}
            </div>

            {overviewError && (
              <div className="feature-card" style={{ marginBottom: '20px', borderColor: 'var(--error)' }}>
                <h3>Overview Error</h3>
                <p>{overviewError}</p>
              </div>
            )}

            <div className="feature-grid" style={{ marginBottom: '20px' }}>
              {dashboardCards.map((card) => (
                <div className="feature-card" key={card.label}>
                  <div className="metric-label">{card.label}</div>
                  <div className="metric-value">{card.value}</div>
                  <div className="metric-detail">{card.detail}</div>
                </div>
              ))}
            </div>

            <div className="two-column-grid">
              <div className="feature-card">
                <h3>System Overview</h3>
                <div className="kv-grid">
                  <span>Host</span><strong>{systemOverview?.hostname ?? '—'}</strong>
                  <span>CPU</span><strong>{systemOverview ? `${systemOverview.cpuCores} cores` : '—'}</strong>
                  <span>Load</span><strong>{systemOverview ? systemOverview.loadAverage.map((value) => value.toFixed(2)).join(' / ') : '—'}</strong>
                  <span>Memory</span><strong>{systemOverview ? `${formatBytes(systemOverview.usedMemoryBytes)} / ${formatBytes(systemOverview.totalMemoryBytes)}` : '—'}</strong>
                  <span>App RSS</span><strong>{systemOverview ? formatBytes(systemOverview.appMemoryBytes) : '—'}</strong>
                  <span>Uptime</span><strong>{systemOverview ? formatUptime(systemOverview.uptimeSeconds) : '—'}</strong>
                </div>
                <p style={{ marginTop: '12px' }}>{systemOverview?.cpuModel ?? 'No system telemetry yet.'}</p>
              </div>

              <div className="feature-card">
                <h3>Database Footprint</h3>
                <div className="kv-grid">
                  <span>Registry</span><strong>{localDbOverview?.registryPresent ? 'Present' : 'Missing'}</strong>
                  <span>Vectors</span><strong>{formatBytes(localDbOverview?.vectorsFileBytes ?? 0)}</strong>
                  <span>Index</span><strong>{formatBytes(localDbOverview?.indexFileBytes ?? 0)}</strong>
                  <span>Metadata</span><strong>{formatBytes(localDbOverview?.metadataFileBytes ?? 0)}</strong>
                  <span>Config</span><strong>{formatBytes(localDbOverview?.configFileBytes ?? 0)}</strong>
                  <span>API Status</span><strong>{apiHealth ? 'Healthy' : 'Offline / auth required'}</strong>
                </div>
              </div>
            </div>

            <div className="feature-card" style={{ marginTop: '20px' }}>
              <div className="section-header">
                <h3>Collections</h3>
                <button className="demo-button" onClick={() => void refreshOverview()} style={{ marginLeft: 0 }}>
                  Refresh
                </button>
              </div>
              <div className="data-table-wrap">
                <table className="data-table">
                  <thead>
                    <tr>
                      <th>Name</th>
                      <th>Dimension</th>
                      <th>Metric</th>
                      <th>Documents</th>
                      <th>Created</th>
                    </tr>
                  </thead>
                  <tbody>
                    {visibleCollections.length > 0 ? visibleCollections.map((collection) => (
                      <tr key={collection.name}>
                        <td>{collection.name}</td>
                        <td>{collection.dimension || '—'}</td>
                        <td>{collection.metric}</td>
                        <td>{collection.documentCount.toLocaleString()}</td>
                        <td>{collection.createdAt ? new Date(collection.createdAt).toLocaleString() : '—'}</td>
                      </tr>
                    )) : (
                      <tr>
                        <td colSpan={5}>No persisted collections found yet.</td>
                      </tr>
                    )}
                  </tbody>
                </table>
              </div>
            </div>

            <div className="feature-card" style={{ marginTop: '20px' }}>
              <h3 style={{ marginBottom: '15px' }}>Studio Workspaces</h3>
              <div className="action-grid">
                <button className="demo-button" onClick={() => setViewMode('search')} style={{ marginLeft: 0 }}>
                  Search Lab
                </button>
                <button className="demo-button" onClick={() => setViewMode('ingest')} style={{ marginLeft: 0 }}>
                  Collections & Ingest
                </button>
                <button className="demo-button" onClick={() => setViewMode('analytics')} style={{ marginLeft: 0 }}>
                  Telemetry Summary
                </button>
                <button className="demo-button" onClick={() => setViewMode('3d')} style={{ marginLeft: 0 }}>
                  Vector Space
                </button>
                <button className="demo-button" onClick={() => setViewMode('quantization')} style={{ marginLeft: 0 }}>
                  PQ Studio
                </button>
              </div>
            </div>
          </div>
        ) : viewMode === 'search' ? (
          <div className="welcome-section">
            {renderBackButton()}
            <h2 style={{ marginBottom: '18px' }}>Search Lab</h2>
            <div className="two-column-grid">
              <div className="feature-card">
                <h3>API Connection</h3>
                <div className="form-grid">
                  <label>
                    API URL
                    <input className="studio-input" value={apiUrl} onChange={(event) => setApiUrl(event.target.value)} />
                  </label>
                  <label>
                    Username
                    <input className="studio-input" value={apiUsername} onChange={(event) => setApiUsername(event.target.value)} />
                  </label>
                  <label>
                    Password
                    <input className="studio-input" type="password" value={apiPassword} onChange={(event) => setApiPassword(event.target.value)} />
                  </label>
                </div>
                <div className="action-grid">
                  <button className="demo-button" onClick={() => void handleConnect()} disabled={isConnecting} style={{ marginLeft: 0 }}>
                    {isConnecting ? 'Connecting…' : authToken ? 'Reconnect' : 'Connect'}
                  </button>
                  {authToken && (
                    <button
                      className="demo-button"
                      style={{ marginLeft: 0, background: 'transparent', border: '1px solid var(--border)' }}
                      onClick={() => {
                        setAuthToken(null);
                        setApiStats(null);
                        setCollections([]);
                        setSearchResults([]);
                      }}
                    >
                      Disconnect
                    </button>
                  )}
                </div>
                <p style={{ marginTop: '12px' }}>{apiStatusMessage}</p>
              </div>

              <div className="feature-card">
                <h3>Query Workspace</h3>
                <div className="form-grid">
                  <label>
                    Collection
                    <select className="studio-input" value={selectedCollection} onChange={(event) => setSelectedCollection(event.target.value)}>
                      <option value="">Select collection</option>
                      {visibleCollections.map((collection) => (
                        <option key={collection.name} value={collection.name}>
                          {collection.name}
                        </option>
                      ))}
                    </select>
                  </label>
                  <label>
                    Query
                    <textarea
                      className="studio-input studio-textarea"
                      value={searchQuery}
                      onChange={(event) => setSearchQuery(event.target.value)}
                      placeholder="Enter a semantic query"
                    />
                  </label>
                  <label>
                    Filter JSON
                    <textarea
                      className="studio-input studio-textarea"
                      value={searchFiltersText}
                      onChange={(event) => setSearchFiltersText(event.target.value)}
                      placeholder='{"year":{"$gte":2024},"language":"en"}'
                    />
                  </label>
                  <label>
                    Top K
                    <input
                      className="studio-input"
                      type="number"
                      min={1}
                      max={100}
                      value={searchTopK}
                      onChange={(event) => setSearchTopK(Number(event.target.value))}
                    />
                  </label>
                </div>
                <button className="demo-button" onClick={() => void handleSearch()} disabled={isSearching} style={{ marginLeft: 0 }}>
                  {isSearching ? 'Searching…' : 'Run Search'}
                </button>
                {searchError && <p style={{ color: 'var(--error)', marginTop: '12px' }}>{searchError}</p>}
              </div>
            </div>

            <div className="feature-card" style={{ marginTop: '20px' }}>
              <h3>Results</h3>
              <div className="data-table-wrap">
                <table className="data-table">
                  <thead>
                    <tr>
                      <th>ID</th>
                      <th>Score</th>
                      <th>Content</th>
                      <th>Metadata</th>
                    </tr>
                  </thead>
                  <tbody>
                    {searchResults.length > 0 ? searchResults.map((result) => (
                      <tr key={result.id}>
                        <td>{result.id}</td>
                        <td>{result.score.toFixed(4)}</td>
                        <td>{result.content || '—'}</td>
                        <td><pre className="json-preview">{JSON.stringify(result.metadata, null, 2)}</pre></td>
                      </tr>
                    )) : (
                      <tr>
                        <td colSpan={4}>No search results yet.</td>
                      </tr>
                    )}
                  </tbody>
                </table>
              </div>
            </div>
          </div>
        ) : viewMode === 'ingest' ? (
          <div className="welcome-section">
            {renderBackButton()}
            <h2 style={{ marginBottom: '18px' }}>Collections & Ingest</h2>
            <div className="two-column-grid">
              <div className="feature-card">
                <h3>Create Collection</h3>
                <div className="form-grid">
                  <label>
                    Name
                    <input
                      className="studio-input"
                      value={collectionDraft.name}
                      onChange={(event) => setCollectionDraft((current) => ({ ...current, name: event.target.value }))}
                    />
                  </label>
                  <label>
                    Dimension
                    <input
                      className="studio-input"
                      type="number"
                      min={1}
                      max={4096}
                      value={collectionDraft.dimension}
                      onChange={(event) => setCollectionDraft((current) => ({ ...current, dimension: Number(event.target.value) }))}
                    />
                  </label>
                  <label>
                    Metric
                    <select
                      className="studio-input"
                      value={collectionDraft.metric}
                      onChange={(event) => setCollectionDraft((current) => ({ ...current, metric: event.target.value }))}
                    >
                      <option value="cosine">cosine</option>
                      <option value="euclidean">euclidean</option>
                      <option value="dot_product">dot_product</option>
                    </select>
                  </label>
                </div>
                <button className="demo-button" onClick={() => void handleCreateCollection()} disabled={isCreatingCollection} style={{ marginLeft: 0 }}>
                  {isCreatingCollection ? 'Creating…' : 'Create Collection'}
                </button>
              </div>

              <div className="feature-card">
                <h3>Add Document</h3>
                <div className="form-grid">
                  <label>
                    Target Collection
                    <select className="studio-input" value={selectedCollection} onChange={(event) => setSelectedCollection(event.target.value)}>
                      <option value="">Select collection</option>
                      {visibleCollections.map((collection) => (
                        <option key={collection.name} value={collection.name}>
                          {collection.name}
                        </option>
                      ))}
                    </select>
                  </label>
                  <label>
                    Document Type
                    <input className="studio-input" value={documentType} onChange={(event) => setDocumentType(event.target.value)} />
                  </label>
                  <label>
                    Content
                    <textarea
                      className="studio-input studio-textarea"
                      value={documentContent}
                      onChange={(event) => setDocumentContent(event.target.value)}
                      placeholder="Paste text to embed and store"
                    />
                  </label>
                  <label>
                    Metadata JSON
                    <textarea
                      className="studio-input studio-textarea"
                      value={documentMetadataText}
                      onChange={(event) => setDocumentMetadataText(event.target.value)}
                      placeholder='{"source":"studio","topic":"demo"}'
                    />
                  </label>
                </div>
                <button className="demo-button" onClick={() => void handleAddDocument()} disabled={isAddingDocument} style={{ marginLeft: 0 }}>
                  {isAddingDocument ? 'Adding…' : 'Add Document'}
                </button>
              </div>
            </div>

            {(managementMessage || managementError) && (
              <div className="feature-card" style={{ marginTop: '20px', borderColor: managementError ? 'var(--error)' : 'var(--accent-color)' }}>
                <h3>Status</h3>
                {managementMessage && <p>{managementMessage}</p>}
                {managementError && <p style={{ color: 'var(--error)' }}>{managementError}</p>}
              </div>
            )}

            <div className="feature-card" style={{ marginTop: '20px' }}>
              <div className="section-header">
                <h3>Existing Collections</h3>
                <button className="demo-button" onClick={() => void refreshOverview()} style={{ marginLeft: 0 }}>
                  Refresh
                </button>
              </div>
              <div className="data-table-wrap">
                <table className="data-table">
                  <thead>
                    <tr>
                      <th>Name</th>
                      <th>Dimension</th>
                      <th>Metric</th>
                      <th>Documents</th>
                      <th>Actions</th>
                    </tr>
                  </thead>
                  <tbody>
                    {visibleCollections.length > 0 ? visibleCollections.map((collection) => (
                      <tr key={collection.name}>
                        <td>{collection.name}</td>
                        <td>{collection.dimension || '—'}</td>
                        <td>{collection.metric}</td>
                        <td>{collection.documentCount.toLocaleString()}</td>
                        <td>
                          <div className="action-grid">
                            <button className="demo-button" style={{ marginLeft: 0 }} onClick={() => {
                              setSelectedCollection(collection.name);
                              setViewMode('search');
                            }}>
                              Search
                            </button>
                            <button
                              className="demo-button danger-button"
                              style={{ marginLeft: 0 }}
                              onClick={() => void handleDeleteCollection(collection.name)}
                              disabled={isDeletingCollection}
                            >
                              {isDeletingCollection ? 'Deleting…' : 'Delete'}
                            </button>
                          </div>
                        </td>
                      </tr>
                    )) : (
                      <tr>
                        <td colSpan={5}>No collections available.</td>
                      </tr>
                    )}
                  </tbody>
                </table>
              </div>
            </div>
          </div>
        ) : viewMode === 'analytics' ? (
          <div className="welcome-section">
            {renderBackButton()}
            <h2 style={{ marginBottom: '18px' }}>Telemetry Summary</h2>
            <div className="feature-grid">
              <div className="feature-card">
                <div className="metric-label">System Memory</div>
                <div className="metric-value">{systemOverview ? formatBytes(systemOverview.usedMemoryBytes) : '—'}</div>
                <div className="metric-detail">{systemOverview ? `of ${formatBytes(systemOverview.totalMemoryBytes)}` : 'Waiting for local telemetry'}</div>
              </div>
              <div className="feature-card">
                <div className="metric-label">App RSS</div>
                <div className="metric-value">{systemOverview ? formatBytes(systemOverview.appMemoryBytes) : '—'}</div>
                <div className="metric-detail">Electron main process</div>
              </div>
              <div className="feature-card">
                <div className="metric-label">Vector Store</div>
                <div className="metric-value">{formatBytes(localDbOverview?.vectorsFileBytes ?? 0)}</div>
                <div className="metric-detail">mmap-backed file size</div>
              </div>
              <div className="feature-card">
                <div className="metric-label">API Uptime</div>
                <div className="metric-value">{apiHealth ? formatUptime(apiHealth.uptime_seconds) : 'Offline'}</div>
                <div className="metric-detail">Health endpoint</div>
              </div>
            </div>

            <div className="two-column-grid" style={{ marginTop: '20px' }}>
              <div className="feature-card">
                <h3>Machine</h3>
                <div className="kv-grid">
                  <span>Platform</span><strong>{systemOverview ? `${systemOverview.platform}/${systemOverview.arch}` : '—'}</strong>
                  <span>CPU</span><strong>{systemOverview?.cpuModel ?? '—'}</strong>
                  <span>Cores</span><strong>{systemOverview?.cpuCores ?? '—'}</strong>
                  <span>Load Avg</span><strong>{systemOverview ? systemOverview.loadAverage.map((value) => value.toFixed(2)).join(' / ') : '—'}</strong>
                  <span>Node</span><strong>{systemOverview?.nodeVersion ?? '—'}</strong>
                  <span>DB Path</span><strong>{config.dbPath || '—'}</strong>
                </div>
              </div>

              <div className="feature-card">
                <h3>Database</h3>
                <div className="kv-grid">
                  <span>Collections</span><strong>{(apiStats?.collections ?? localDbOverview?.collectionCount ?? 0).toLocaleString()}</strong>
                  <span>Tracked Docs</span><strong>{(localDbOverview?.trackedDocuments ?? 0).toLocaleString()}</strong>
                  <span>Vectors</span><strong>{(apiStats?.total_vectors ?? 0).toLocaleString()}</strong>
                  <span>Index Size</span><strong>{formatBytes(apiStats?.index_size ?? localDbOverview?.indexFileBytes ?? 0)}</strong>
                  <span>Memory Usage</span><strong>{formatBytes(apiStats?.memory_usage_bytes ?? 0)}</strong>
                  <span>Native</span><strong>{nativeStatus}</strong>
                </div>
              </div>
            </div>
          </div>
        ) : viewMode === 'quantization' ? (
          <div style={{ height: '100%', display: 'flex', flexDirection: 'column' }}>
            <div
              style={{
                display: 'flex',
                justifyContent: 'space-between',
                alignItems: 'center',
                padding: '10px 12px',
                borderBottom: '1px solid rgba(255,255,255,0.1)',
              }}
            >
              <button
                onClick={() => setViewMode('dashboard')}
                className="demo-button"
                style={{ background: 'transparent', border: '1px solid var(--border)' }}
              >
                ← Back to Dashboard
              </button>
              <h2 style={{ margin: 0, fontSize: '16px' }}>
                ⭐ Perceptual Quantization Studio
              </h2>
              <div style={{ fontSize: '11px', opacity: 0.6 }}>
                SMPTE ST 2084 | Dolby Vision | HDR10 | Netflix Ready
              </div>
            </div>
            <div style={{ flex: 1, minHeight: 0 }}>
              <PerceptualQuantizationPanel
                vectors={demoVectors.map((vector) => ({
                  id: vector.id,
                  values: [...vector.position, vector.distance, vector.position[0], vector.position[1], vector.position[2]],
                }))}
                onQuantize={async (panelConfig) => {
                  console.log('Quantizing with config:', panelConfig);
                  await new Promise((resolve) => setTimeout(resolve, 1000));
                }}
                onExport={(format) => {
                  console.log('Exporting as:', format);
                }}
              />
            </div>
          </div>
        ) : viewMode === '3d' ? (
          <div>
            {renderBackButton()}
            <div className="demo-container">
              <VectorSpace3D
                vectors={demoVectors}
                geometry="euclidean"
                onVectorClick={(vector) => console.log('Clicked vector:', vector)}
              />
            </div>
          </div>
        ) : (
          <div className="welcome-section">{renderBackButton()}</div>
        )}
      </main>

      <footer className="app-footer">
        <p>
          HEKTOR Studio | Collections {visibleCollections.length} | API {apiHealth ? 'Online' : 'Offline'} | {currentTheme.displayName}
        </p>
      </footer>
    </div>
  );
}

declare global {
  interface Window {
    electronAPI?: {
      getAppVersion: () => Promise<string>;
      getTheme: (themeName: string) => Promise<unknown>;
      getStudioConfig: () => Promise<StudioConfig>;
      getSystemOverview: () => Promise<SystemOverview>;
      getLocalDatabaseOverview: () => Promise<LocalDatabaseOverview>;
      onThemeChanged: (callback: (theme: unknown) => void) => void;
    };
    hektorAPI?: {
      openDatabase: (path: string) => unknown;
      closeDatabase: () => unknown;
      addVector: (vector: number[], metadata?: Record<string, unknown>) => unknown;
      queryVectors: (vector: number[], topK?: number) => unknown;
      queryVectorsAsync: (vector: number[], topK: number, callback: Function) => void;
      getVersion: () => unknown;
      isNativeAvailable: () => boolean;
    };
  }
}

export default App;
